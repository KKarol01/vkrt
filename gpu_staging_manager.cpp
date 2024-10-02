#include "gpu_staging_manager.hpp"
#include "free_list_allocator.hpp"
#include "renderer_vulkan.hpp"

GpuStagingManager::GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes)
    : submit_queue{ new QueueScheduler{ queue } }, queue_idx{ queue_index } {
    assert(submit_queue);

    vks::BufferCreateInfo binfo;
    binfo.size = pool_size_bytes;
    binfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo vinfo{ .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO,
                                   .preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT };

    pool_memory = new Buffer{ "staging buffer", binfo, vinfo, 1u };
    pool = new FreeListAllocator{ pool_memory->mapped, pool_size_bytes };

    assert(pool && "Pool creation failure");
    if(pool) {
        const size_t biggest_size = pool->try_get_best_fit_size(pool_size_bytes);
        assert(biggest_size > 0);
        void* alloc = pool->allocate(biggest_size);
        assert(alloc);
        pool->deallocate(alloc);

        stage_thread = std::jthread{ &GpuStagingManager::submit_uploads, this };
        stop_token = stage_thread.get_stop_token();
    }

    cmdpool = new CommandPool{ queue_index };
}

GpuStagingManager::~GpuStagingManager() noexcept {
    if(stop_token.stop_possible()) {
        stage_thread.request_stop();
        thread_cvar.notify_one();
        if(stage_thread.joinable()) { stage_thread.join(); }
    }
    if(pool) {
        delete pool;
        pool = nullptr;
    }
    if(pool_memory) {
        delete pool_memory;
        pool_memory = nullptr;
    }
    delete submit_queue;

    auto* renderer = RendererVulkan::get();
    delete cmdpool;
}

bool GpuStagingManager::send_to(VkBuffer dst, size_t dst_offset, std::span<const std::byte> src, std::atomic_flag* flag) {
    return send_to_impl(dst, dst_offset, std::vector<std::byte>{ src.begin(), src.end() }, 0ull, src.size_bytes(), flag);
}

bool GpuStagingManager::send_to(VkBuffer dst, size_t dst_offset, VkBuffer src, size_t src_offset, size_t size,
                                std::atomic_flag* flag) {
    return send_to_impl(dst, dst_offset, src, src_offset, size, flag);
}

bool GpuStagingManager::send_to(Image* dst, VkOffset3D dst_offset, std::span<const std::byte> src,
                                VkSemaphore src_release_sem, uint32_t src_queue_idx, std::atomic_flag* flag) {
    return send_to_impl(dst, VkImageSubresourceLayers{ .aspectMask = dst->aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
                        dst_offset, VkExtent3D{ .width = dst->width, .height = dst->height, .depth = dst->depth }, src,
                        0, 0, src_release_sem, src_queue_idx, flag);
}

bool GpuStagingManager::send_to_impl(VkBuffer dst, size_t dst_offset, std::variant<std::span<const std::byte>, VkBuffer>&& src,
                                     size_t src_offset, size_t size, std::atomic_flag* flag) {
    if(flag) { flag->clear(); }
    if(!dst || (src.index() == 1 && !std::get<1>(src))) { return false; }
    if(size == 0) {
        if(flag) { flag->test_and_set(); }
        return true;
    }
    ResourceType dst_type = BUFFER;
    ResourceType src_type = src.index() == 0 ? BYTE_SPAN : BUFFER;
    std::scoped_lock lock{ queue_mutex };
    transactions.push_front(Transaction{ .on_complete_flag = flag,
                                         .dst = { .buffer = dst },
                                         .src = [&src, &src_type]() -> decltype(Transaction::src) {
                                             if(src_type == BYTE_SPAN) {
                                                 const auto size = std::get<0>(src).size_bytes();
                                                 std::byte* memory = static_cast<std::byte*>(malloc(size));
                                                 assert(memory);
                                                 memcpy(memory, std::get<0>(src).data(), size);
                                                 return { .byte_span = std::make_pair(memory, size) };
                                             } else {
                                                 return { .buffer = std::get<1>(src) };
                                             }
                                         }(),
                                         .dst_offset{ .buffer_offset = dst_offset },
                                         .src_offset = { .buffer_offset = src_offset },
                                         .uploaded = 0ull,
                                         .upload_size = size,
                                         .dst_type = dst_type,
                                         .src_type = src_type });
    queue.push(&transactions.front());
    thread_cvar.notify_one();
    return true;
}

bool GpuStagingManager::send_to_impl(Image* dst_image, VkImageSubresourceLayers dst_subresource, VkOffset3D dst_offset,
                                     VkExtent3D dst_extent, std::span<const std::byte> src, uint32_t src_row_length,
                                     uint32_t src_image_height, VkSemaphore src_release_semaphore,
                                     uint32_t src_queue_idx, std::atomic_flag* flag) {
    if(flag) { flag->clear(); }

    if(dst_extent.width == 0 || dst_extent.height == 0 || dst_extent.depth == 0) {
        if(flag) { flag->test_and_set(); }
        return true;
    }

    std::scoped_lock lock{ queue_mutex, cmdpool_mutex };
    VkCommandBuffer cmd = cmdpool->begin_onetime();
    ++allocated_command_buffers;
    VkImageMemoryBarrier src_image_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_NONE,
        .dstAccessMask = VK_ACCESS_NONE,
        .oldLayout = dst_image->current_layout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = src_queue_idx,
        .dstQueueFamilyIndex = queue_idx,
        .image = dst_image->image,
        .subresourceRange = { .aspectMask = dst_subresource.aspectMask, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0, {}, 1, &src_image_barrier);
    cmdpool->end(cmd);
    VkSemaphore image_dst_acquire_sem = RendererVulkan::get()->create_semaphore();
    submit_queue->enqueue_wait_submit({ .buffers = { cmd },
                                        .waits = { { src_release_semaphore, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
                                        .signals = { { image_dst_acquire_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });

    dst_image->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    std::byte* byte_span = static_cast<std::byte*>(malloc(src.size_bytes()));
    assert(byte_span);
    memcpy(byte_span, src.data(), src.size_bytes());
    transactions.push_front(Transaction{ .on_complete_flag = flag,
                                         .dst = { .image = dst_image->image },
                                         .src = { .byte_span = std::make_pair(byte_span, src.size_bytes()) },
                                         .dst_offset = { .image_offset = dst_offset },
                                         .uploaded = 0,
                                         .upload_size = src.size_bytes(),
                                         .src_queue_idx = src_queue_idx,
                                         .image_block_size = 4,
                                         .image_extent = dst_extent,
                                         .image_subresource = dst_subresource,
                                         .image_dst_acquire_sem = image_dst_acquire_sem,
                                         .dst_type = IMAGE,
                                         .src_type = BYTE_SPAN });
    queue.push(&transactions.front());
    thread_cvar.notify_one();
    return true;
}

void GpuStagingManager::schedule_upload() {
    for(; !queue.empty();) {
        Transaction& t = *queue.front();
        size_t remaining = t.get_remaining();
        size_t offset = 0;
        for(;;) {
            if(t.src_type == BUFFER) {
                uploads.push_back(Upload{
                    .t = &t,
                    .copy_region = { .buffer = { .srcOffset = t.src_offset.buffer_offset,
                                                 .dstOffset = t.dst_offset.buffer_offset,
                                                 .size = t.get_size() } },
                    .src_storage = t.src.buffer,
                });
                queue.pop();
                break;
            }

            if(pool->get_total_free_memory() == 0) { return; }
            if(remaining == 0) {
                queue.pop();
                break;
            }

            const size_t fit = pool->try_get_best_fit_size(remaining);
            if(fit == 0) { return; }

            size_t upload_size = std::min(fit, remaining);

            size_t image_row_byte_size = 0;
            if(t.dst_type == IMAGE) {
                // Only uploading full image rows.
                image_row_byte_size = t.image_block_size * t.image_extent.width;
                upload_size = upload_size - upload_size % image_row_byte_size;
                if(upload_size == 0) { return; }
            }

            void* alloc = pool->allocate(upload_size);
            if(!alloc) {
                assert(false);
                return;
            }

            memcpy(alloc, t.src.byte_span.first + t.uploaded + offset, upload_size);

            Upload& upload = uploads.emplace_back();
            upload.t = &t;
            upload.src_storage = alloc;
            if(t.dst_type == BUFFER) {
                upload.copy_region = { .buffer = {
                                           .srcOffset = pool->get_offset_bytes(alloc),
                                           .dstOffset = t.dst_offset.buffer_offset + t.uploaded + offset,
                                           .size = upload_size,
                                       } };
            } else if(t.dst_type == IMAGE) {
                upload.copy_region = { .image = { .bufferOffset = pool->get_offset_bytes(alloc),
                                                  .imageSubresource = t.image_subresource,
                                                  .imageOffset = { .y = static_cast<int>((t.uploaded + offset) / image_row_byte_size) },
                                                  .imageExtent = { .width = t.image_extent.width,
                                                                   .height = static_cast<uint32_t>(upload_size / image_row_byte_size),
                                                                   .depth = 1u } } };
            } else {
                assert(false && "Unrecognized type...");
                queue.pop();
                break;
            }
            remaining -= upload_size;
            offset += upload_size;
        }
    }
}

void GpuStagingManager::submit_uploads() {
    while(!stop_token.stop_requested()) {
        std::unique_lock lock{ queue_mutex };
        thread_cvar.wait(lock, [this] { return !queue.empty() || stop_token.stop_requested(); });
        auto* renderer = RendererVulkan::get();
        schedule_upload();
        auto uploads = std::move(this->uploads);
        this->uploads.clear();

        if(allocated_command_buffers.load() >= 128) {
            int val;
            while((val = background_task_count.load()) > 0) {
                background_task_count.wait(val);
            }
            cmdpool->reset();
            allocated_command_buffers = 0;
        }

        if(uploads.empty()) { continue; }
        lock.unlock();

        std::unique_lock cmdpool_lock{ cmdpool_mutex };
        VkCommandBuffer cmd = cmdpool->begin_onetime();
        VkFence fence{};
        VkFenceCreateInfo fence_info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VK_CHECK(vkCreateFence(renderer->dev, &fence_info, {}, &fence));
        std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> wait_sems;
        for(const auto& e : uploads) {
            if(e.t->dst_type == BUFFER) {
                if(e.is_src_allocation()) {
                    vkCmdCopyBuffer(cmd, pool_memory->buffer, e.t->dst.buffer, 1, &e.copy_region.buffer);
                } else if(e.is_src_vkbuffer()) {
                    vkCmdCopyBuffer(cmd, std::get<1>(e.src_storage)->buffer, e.t->dst.buffer, 1, &e.copy_region.buffer);
                }
            } else if(e.t->dst_type == IMAGE) {
                if(!e.is_src_allocation()) {
                    assert(false);
                    continue;
                }
                vkCmdCopyBufferToImage(cmd, pool_memory->buffer, e.t->dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1, &e.copy_region.image);
                if(e.t->get_remaining() == e.get_size(*this)) {
                    VkImageMemoryBarrier barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                  .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                                  .dstAccessMask = VK_ACCESS_NONE,
                                                  .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .srcQueueFamilyIndex = queue_idx,
                                                  .dstQueueFamilyIndex = e.t->src_queue_idx,
                                                  .image = e.t->dst.image,
                                                  .subresourceRange = { .aspectMask = e.t->image_subresource.aspectMask,
                                                                        .baseMipLevel = 0,
                                                                        .levelCount = 1,
                                                                        .baseArrayLayer = 0,
                                                                        .layerCount = 1 } };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0,
                                         {}, 1, &barrier);
                }
                if(e.t->wait_on_sem) {
                    e.t->wait_on_sem = false;
                    wait_sems.emplace_back(e.t->image_dst_acquire_sem, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
                }
            }
        }
        cmdpool->end(cmd);
        submit_queue->enqueue({ .buffers = { cmd }, .waits = wait_sems }, fence);
        cmdpool_lock.unlock();
        ++allocated_command_buffers;
        ++background_task_count;
        std::thread clear_thread{ [&mgr = *this, &queue_mutex = this->queue_mutex, &pool = this->pool,
                                   &transactions = this->transactions, &thread_cvar = this->thread_cvar,
                                   &cmdpool = this->cmdpool, &background_task_count = this->background_task_count,
                                   uploads = std::move(uploads), cmd, fence, renderer = RendererVulkan::get()] {
            VK_CHECK(vkWaitForFences(
                renderer->dev, 1, &fence, true,
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{ 100 }).count()));

            {
                std::scoped_lock lock{ queue_mutex };
                for(const auto& e : uploads) {
                    const auto s = e.get_size(mgr);
                    e.t->uploaded += e.get_size(mgr);
                    if(e.src_storage.index() == 0) { pool->deallocate(std::get<0>(e.src_storage)); }
                }
                for(auto it = transactions.before_begin(); it != transactions.end(); ++it) {
                    if(auto n = std::next(it); n != transactions.end() && n->get_remaining() == 0) {
                        if(n->image_dst_acquire_sem) { renderer->destroy_semaphore(n->image_dst_acquire_sem); }
                        if(n->on_complete_flag) {
                            n->on_complete_flag->test_and_set();
                            n->on_complete_flag->notify_all();
                        }
                        transactions.erase_after(it);
                    }
                }
            }
            vkDestroyFence(renderer->dev, fence, nullptr);
            --background_task_count;
            background_task_count.notify_all();
            thread_cvar.notify_one();
        } };
        clear_thread.detach();
    }
}

constexpr size_t GpuStagingManager::Transaction::get_remaining() const { return get_size() - uploaded; }

constexpr size_t GpuStagingManager::Transaction::get_size() const { return upload_size; }

constexpr size_t GpuStagingManager::Upload::get_size(const GpuStagingManager& mgr) const {
    if(is_src_allocation()) {
        return mgr.pool->get_alloc_data_size(std::get<0>(src_storage));
    } else if(is_src_vkbuffer()) {
        return std::get<1>(src_storage)->size;
    }
}
