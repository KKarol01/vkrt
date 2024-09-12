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

    vks::CommandPoolCreateInfo cmdpool_info;
    cmdpool_info.queueFamilyIndex = queue_index;
    VK_CHECK(vkCreateCommandPool(RendererVulkan::get()->dev, &cmdpool_info, {}, &cmdpool));
    assert(cmdpool);
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
    if(cmdpool) {
        vkResetCommandPool(renderer->dev, cmdpool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
        vkDestroyCommandPool(renderer->dev, cmdpool, {});
    }
}

bool GpuStagingManager::send_to(VkBuffer dst, size_t dst_offset, std::span<const std::byte> src, std::atomic_flag* flag) {
    return send_to_impl(dst, dst_offset, std::vector<std::byte>{ src.begin(), src.end() }, flag);
}

bool GpuStagingManager::send_to(VkBuffer dst, size_t dst_offset, const Buffer* src, std::atomic_flag* flag) {
    return send_to_impl(dst, dst_offset, src, flag);
}

bool GpuStagingManager::send_to(Image* dst, VkOffset3D dst_offset, std::span<const std::byte> src, VkCommandBuffer src_cmd,
                                QueueScheduler* src_queue, uint32_t src_queue_idx, std::atomic_flag* flag) {
    return send_to_impl(dst, VkImageSubresourceLayers{ .aspectMask = dst->aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
                        dst_offset, VkExtent3D{ .width = dst->width, .height = dst->height, .depth = dst->depth }, src,
                        0, 0, src_cmd, src_queue, src_queue_idx, flag);
}

bool GpuStagingManager::send_to_impl(VkBuffer dst, size_t dst_offset,
                                     std::variant<std::vector<std::byte>, const Buffer*>&& src, std::atomic_flag* flag) {
    if(flag) { flag->clear(); }

    if(src.index() == 1 && !std::get<1>(src)) { return false; }
    if((src.index() == 0 && std::get<0>(src).size() == 0) || (src.index() == 1 && std::get<1>(src) && std::get<1>(src)->size == 0)) {
        if(flag) { flag->test_and_set(std::memory_order_relaxed); }
        return true;
    }

    std::scoped_lock lock{ queue_mutex };
    transactions.push_front(Transaction{
        .flag = flag,
        .src = std::move(src),
        .dst = dst,
        .dst_offset = dst_offset,
        .uploaded = 0,
    });
    queue.push(&transactions.front());
    thread_cvar.notify_one();
    return true;
}

bool GpuStagingManager::send_to_impl(Image* dst_image, VkImageSubresourceLayers dst_subresource, VkOffset3D dst_offset,
                                     VkExtent3D dst_extent, std::span<const std::byte> src, uint32_t src_row_length,
                                     uint32_t src_image_height, VkCommandBuffer src_cmd, QueueScheduler* src_queue,
                                     uint32_t src_queue_idx, std::atomic_flag* flag) {
    if(flag) { flag->clear(); }

    if(dst_extent.width == 0 || dst_extent.height == 0 || dst_extent.depth == 0) {
        if(flag) { flag->test_and_set(std::memory_order_relaxed); }
        return true;
    }

    vks::SemaphoreCreateInfo sem_info;
    VkSemaphore image_src_release_sem{}, image_dst_acquire_sem{};
    VK_CHECK(vkCreateSemaphore(RendererVulkan::get()->dev, &sem_info, nullptr, &image_src_release_sem));
    VK_CHECK(vkCreateSemaphore(RendererVulkan::get()->dev, &sem_info, nullptr, &image_dst_acquire_sem));

    vks::CommandBufferBeginInfo src_begin_info;
    src_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(src_cmd, &src_begin_info));
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
    vkCmdPipelineBarrier(src_cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0, {}, 1, &src_image_barrier);
    VK_CHECK(vkEndCommandBuffer(src_cmd));

    src_queue->enqueue_wait_submit({ .buffers = { src_cmd },
                                     .signals = { { image_src_release_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });

    std::scoped_lock lock{ queue_mutex };

    VkCommandBuffer cmd{};
    vks::CommandBufferAllocateInfo alloc_info;
    alloc_info.commandPool = cmdpool;
    alloc_info.commandBufferCount = 1;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VK_CHECK(vkAllocateCommandBuffers(RendererVulkan::get()->dev, &alloc_info, &cmd));

    VK_CHECK(vkBeginCommandBuffer(cmd, &src_begin_info));
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0, {}, 1, &src_image_barrier);
    VK_CHECK(vkEndCommandBuffer(cmd));
    submit_queue->enqueue_wait_submit({ .buffers = { cmd },
                                        .waits = { { image_src_release_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
                                        .signals = { { image_dst_acquire_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });
    allocated_command_buffers.fetch_add(1, std::memory_order_relaxed);

    dst_image->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    transactions.push_front(Transaction{
        .flag = flag,
        .src = std::vector<std::byte>{ src.begin(), src.end() },
        .dst = dst_image->image,
        .dst_offset = dst_offset,
        .uploaded = 0,
        .image_block_size = 4,
        .image_extent = dst_extent,
        .image_subresource = dst_subresource,
        .image_src_release_sem = image_src_release_sem,
        .image_dst_acquire_sem = image_dst_acquire_sem,
    });
    queue.push(&transactions.front());
    thread_cvar.notify_one();
    return true;
}

void GpuStagingManager::schedule_upload() {
    for(;;) {
        if(queue.empty()) { return; }

        Transaction& t = *queue.front();

        size_t remaining = t.get_remaining();
        size_t offset = 0;
        for(;;) {
            if(t.is_src_vkbuffer()) {
                uploads.push_back(Upload{
                    .t = &t,
                    .region =
                        VkBufferCopy{
                            .srcOffset = 0,
                            .dstOffset = std::get<0>(t.dst_offset),
                            .size = t.get_size(),
                        },
                    .src_storage = std::get<1>(t.src),
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
            if(t.is_dst_vkimage()) {
                image_row_byte_size = t.image_block_size * t.image_extent.width;
                upload_size = upload_size - upload_size % image_row_byte_size;
                if(upload_size == 0) { return; }
            }

            void* alloc = pool->allocate(upload_size);
            if(!alloc) {
                assert(false);
                return;
            }

            memcpy(alloc, std::get<0>(t.src).data() + t.uploaded + offset, upload_size);

            Upload& upload = uploads.emplace_back();
            upload.t = &t;
            if(t.is_dst_vkbuffer()) {
                upload.region = VkBufferCopy{
                    .srcOffset = pool->get_offset_bytes(alloc),
                    .dstOffset = std::get<0>(t.dst_offset) + t.uploaded + offset,
                    .size = upload_size,
                };
            } else if(t.is_dst_vkimage()) {
                upload.region =
                    VkBufferImageCopy{ .bufferOffset = pool->get_offset_bytes(alloc),
                                       .imageSubresource = t.image_subresource,
                                       .imageOffset = { .y = static_cast<int>((t.uploaded + offset) / image_row_byte_size) },
                                       .imageExtent = { .width = t.image_extent.width,
                                                        .height = static_cast<uint32_t>(upload_size / image_row_byte_size),
                                                        .depth = 1u } };
            } else {
                assert(false && "Unrecognized type...");
                queue.pop();
                break;
            }
            upload.src_storage = alloc;

            remaining -= upload_size;
            offset += upload_size;
        }
    }
}

void GpuStagingManager::submit_uploads() {
    while(!stop_token.stop_requested()) {
        std::unique_lock lock{ queue_mutex };
        thread_cvar.wait(lock, [this] { return !queue.empty() || stop_token.stop_requested(); });

        if(stop_token.stop_requested()) { return; }

        auto* renderer = RendererVulkan::get();

        schedule_upload();
        auto uploads = std::move(this->uploads);
        this->uploads.clear();

        lock.unlock();

        if(allocated_command_buffers.load(std::memory_order_relaxed) >= 1024) {
            int val;
            while((val = background_task_count.load(std::memory_order_relaxed)) > 0) {
                background_task_count.wait(val, std::memory_order_relaxed);
            }
            VK_CHECK(vkResetCommandPool(renderer->dev, cmdpool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));
            allocated_command_buffers = 0;
        }

        if(uploads.empty()) { continue; }

        VkCommandBuffer cmd{};
        vks::CommandBufferAllocateInfo alloc_info;
        alloc_info.commandPool = cmdpool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_CHECK(vkAllocateCommandBuffers(renderer->dev, &alloc_info, &cmd));
        if(!cmd) {
            ENG_WARN("GpuStagingManager could not allocate command buffer.");
            continue;
        }

        std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> wait_sems;

        VkFence fence{};
        VkFenceCreateInfo fence_info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VK_CHECK(vkCreateFence(renderer->dev, &fence_info, {}, &fence));

        vks::CommandBufferBeginInfo begin_info;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));
        for(const auto& e : uploads) {
            if(e.t->is_dst_vkbuffer()) {
                if(e.is_src_allocation()) {
                    vkCmdCopyBuffer(cmd, pool_memory->buffer, std::get<0>(e.t->dst), 1, &std::get<0>(e.region));
                } else if(e.is_src_vkbuffer()) {
                    vkCmdCopyBuffer(cmd, std::get<1>(e.src_storage)->buffer, std::get<0>(e.t->dst), 1, &std::get<0>(e.region));
                }
            } else if(e.t->is_dst_vkimage()) {
                if(!e.is_src_allocation()) {
                    assert(false);
                    continue;
                }
                vkCmdCopyBufferToImage(cmd, pool_memory->buffer, std::get<1>(e.t->dst),
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &std::get<1>(e.region));

                if(e.t->wait_on_sem) {
                    e.t->wait_on_sem = false;
                    wait_sems.emplace_back(e.t->image_dst_acquire_sem, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
                }
            }
        }
        VK_CHECK(vkEndCommandBuffer(cmd));

        ++allocated_command_buffers;

        submit_queue->enqueue({ .buffers = { cmd }, .waits = wait_sems }, fence);

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
                    e.t->uploaded += e.get_size(mgr);
                    if(e.src_storage.index() == 0) { pool->deallocate(std::get<0>(e.src_storage)); }
                }
                for(auto it = transactions.before_begin(); it != transactions.end(); ++it) {
                    if(auto n = std::next(it); n != transactions.end() && n->get_remaining() == 0) {
                        if(n->image_src_release_sem) {
                            vkDestroySemaphore(renderer->dev, n->image_src_release_sem, nullptr);
                            vkDestroySemaphore(renderer->dev, n->image_dst_acquire_sem, nullptr);
                        }
                        if(n->flag) {
                            n->flag->test_and_set(std::memory_order_relaxed);
                            n->flag->notify_all();
                        }
                        transactions.erase_after(it);
                    }
                }
            }

            vkDestroyFence(renderer->dev, fence, nullptr);

            background_task_count.fetch_sub(1, std::memory_order_relaxed);
            background_task_count.notify_all();
            thread_cvar.notify_one();
        } };
        clear_thread.detach();
    }
}

constexpr size_t GpuStagingManager::Transaction::get_remaining() const { return get_size() - uploaded; }

constexpr size_t GpuStagingManager::Transaction::get_size() const {
    if(src.index() == 0) {
        return std::get<0>(src).size();
    } else {
        return std::get<1>(src)->size;
    }
}

constexpr size_t GpuStagingManager::Upload::get_size(const GpuStagingManager& mgr) const {
    if(is_src_allocation()) {
        return mgr.pool->get_alloc_data_size(std::get<0>(src_storage));
    } else if(is_src_vkbuffer()) {
        return std::get<0>(region).size;
    }
}
