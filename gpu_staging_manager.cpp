#include "gpu_staging_manager.hpp"
#include "linear_allocator.hpp"
#include "renderer_vulkan.hpp"
#include <map>

GpuStagingManager::GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes)
    : submit_queue{ new QueueScheduler{ queue } }, queue_idx{ queue_index } {
    assert(submit_queue);

    vks::BufferCreateInfo binfo;
    binfo.size = pool_size_bytes;
    binfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo vinfo{ .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO };

    pool_memory = new Buffer{ "staging buffer", binfo, vinfo, 1u };
    assert(pool_memory && pool_memory->buffer && pool_memory->mapped);
    if(pool_memory && pool_memory->buffer && pool_memory->mapped) {
        pool = new LinearAllocator{ pool_memory->mapped, pool_size_bytes };
    }
    assert(pool && "Pool creation failure");
    if(pool) {
        const size_t biggest_size = pool->get_free_space();
        assert(biggest_size > 0);
        void* alloc = pool->allocate(biggest_size);
        assert(alloc);
        pool->free();
        if(alloc) {
            stage_thread = std::jthread{ &GpuStagingManager::submit_uploads, this };
            stop_token = stage_thread.get_stop_token();
        }
    }
    cmdpool = new CommandPool{ queue_index };
    assert(cmdpool);
}

GpuStagingManager::~GpuStagingManager() noexcept {
    if(stop_token.stop_possible()) {
        stage_thread.request_stop();
        thread_cvar.notify_one();
        if(stage_thread.joinable()) { stage_thread.join(); }
    }
    delete std::exchange(pool, nullptr);
    delete std::exchange(pool_memory, nullptr);
    delete submit_queue;
    delete cmdpool;
}

bool GpuStagingManager::send_to(std::span<const GpuStagingUpload> uploads, VkSemaphore wait_sem, VkSemaphore signal_sem,
                                std::atomic_flag* flag) {
    std::lock_guard lock{ queue_mutex };
    VkCommandBuffer cmd{};
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signal_sems;
    std::shared_ptr<std::atomic_int> completed_transactions = std::make_unique<std::atomic_int>(0);
    if(flag) { flag->clear(); }
    uint32_t bulk_offset = 0;
    signal_sems.reserve(uploads.size());

    if(signal_sem) { cmd = get_command_buffer(false); }

    for(const auto& upload : uploads) {
        if(upload.size_bytes == 0) {
            ++bulk_offset;
            continue;
        }

        transactions.push_front(Transaction{
            .dst = [&upload]() -> decltype(Transaction::dst) {
                if(auto* const buffer = std::get_if<VkBuffer>(&upload.dst)) {
                    return *buffer;
                } else if(auto* const* img = std::get_if<Image*>(&upload.dst)) {
                    return (*img)->image;
                }
            }(),
            .src = [&upload]() -> decltype(Transaction::src) {
                if(const auto* buffer = std::get_if<VkBuffer>(&upload.src)) {
                    return *buffer;
                } else if(const auto* span = std::get_if<std::span<const std::byte>>(&upload.src)) {
                    size_t size = span->size_bytes();
                    std::byte* memory = static_cast<std::byte*>(malloc(size));
                    assert(memory);
                    memcpy(memory, span->data(), size);
                    return std::make_pair(memory, size);
                }
            }(),
            .dst_offset = [&upload]() -> decltype(Transaction::dst_offset) {
                if(const auto* sizet = std::get_if<size_t>(&upload.dst_offset)) {
                    return *sizet;
                } else if(const auto* vkoff = std::get_if<VkOffset3D>(&upload.dst_offset)) {
                    return *vkoff;
                }
            }(),
            .src_offset = { .buffer_offset = upload.src_offset },
            .signal_semaphore = signal_sem,
            .on_complete_flag = flag,
            .completed_transactions = completed_transactions,
            .transactions_in_bulk = (uint32_t)uploads.size(),
            .uploaded = 0u,
            .upload_size = upload.size_bytes,
            .src_queue_idx = upload.src_queue_idx,
            .image_block_size = 4u, /* Currently hardcoded for rgba8 */
            .image_extent = [&upload]() -> VkExtent3D {
                if(Image* const* image = std::get_if<Image*>(&upload.dst)) {
                    return VkExtent3D{ .width = (*image)->width, .height = (*image)->height, .depth = (*image)->depth };
                }
                return {};
            }(),
            .image_subresource = [&upload]() -> VkImageSubresourceLayers {
                if(Image* const* image = std::get_if<Image*>(&upload.dst)) {
                    return { .aspectMask = (*image)->aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 };
                }
                return {};
            }(),
            .dst_type = upload.dst.index() == 0 ? BUFFER : IMAGE,
            .src_type = upload.src.index() == 0 ? BUFFER : BYTE_SPAN,
        });

        if(transactions.front().dst_type == IMAGE) {
            Image* image = std::get<1>(upload.dst);
            VkImageMemoryBarrier src_image_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask = VK_ACCESS_NONE,
                .oldLayout = image->current_layout,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = upload.src_queue_idx,
                .dstQueueFamilyIndex = queue_idx,
                .image = image->image,
                .subresourceRange = { .aspectMask = image->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };
            transactions.front().image_dst_acq_sem = get_renderer().create_semaphore();
            signal_sems.emplace_back(transactions.front().image_dst_acq_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            if(!cmd) { cmd = get_command_buffer(false); }
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0, {}, 1, &src_image_barrier);
            image->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        queue.push(&transactions.front());
    }

    const bool all_uploads_empty = bulk_offset == uploads.size();
    if(all_uploads_empty) {
        signal_sems.emplace_back(signal_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }

    if(cmd) {
        cmdpool->end(cmd);
        submit_queue->enqueue_wait_submit({ .buffers = { cmd },
                                            .waits = wait_sem ? std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>>{ { wait_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } }
                                                              : std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>>{},
                                            .signals = signal_sems });
    }

    {
        if(all_uploads_empty) {
            if(flag) { flag->test_and_set(); }
            return true;
        }

        auto it = transactions.begin();
        for(uint32_t i = 0; i < uploads.size() - bulk_offset; ++i) {
            it->transactions_in_bulk -= bulk_offset;
            ++it;
        }
    }

    thread_cvar.notify_all();
    return true;
}

bool GpuStagingManager::send_to(const GpuStagingUpload& upload, VkSemaphore wait_sem, VkSemaphore signal_sem, std::atomic_flag* flag) {
    return send_to(std::span{ &upload, 1 }, wait_sem, signal_sem, flag);
}

void GpuStagingManager::schedule_upload() {
    while(!queue.empty()) {
        Transaction& t = *queue.front();
        size_t remaining = t.get_remaining();
        size_t offset = 0;

        for(;;) {
            if(t.src_type == BUFFER) {
                uploads.push_back(Upload{ .t = &t,
                                          .copy_region = { .buffer = { .srcOffset = t.src_offset.buffer_offset,
                                                                       .dstOffset = t.dst_offset.buffer_offset,
                                                                       .size = t.get_size() } },
                                          .src_storage = t.src.buffer,
                                          .is_final = true });
                queue.pop();
                break;
            }

            if(pool->get_free_space() == 0) { return; }
            if(remaining == 0) {
                queue.pop();
                break;
            }

            const size_t fit = pool->get_free_space();
            size_t upload_size = std::min(fit, remaining);

            size_t image_row_byte_size = 0;
            if(t.dst_type == IMAGE) {
                // Only uploading full image rows.
                image_row_byte_size = t.image_block_size * t.image_extent.width;
                upload_size = upload_size - upload_size % image_row_byte_size;
                if(upload_size == 0) { return; }
            }

            void* alloc = pool->allocate(upload_size);
            assert(alloc);
            memcpy(alloc, t.src.byte_span.first + t.uploaded + offset, upload_size);

            Upload& upload = uploads.emplace_back();
            upload.t = &t;
            upload.src_storage = std::make_pair(alloc, upload_size);
            upload.is_final = upload_size == remaining;
            if(t.dst_type == BUFFER) {
                upload.copy_region = { .buffer = {
                                           .srcOffset = pool->get_byte_offset(alloc),
                                           .dstOffset = t.dst_offset.buffer_offset + t.uploaded + offset,
                                           .size = upload_size,
                                       } };
            } else if(t.dst_type == IMAGE) {
                upload.copy_region = { .image = { .bufferOffset = pool->get_byte_offset(alloc),
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
            cmdpool->reset();
            allocated_command_buffers = 0;
        }

        if(uploads.empty()) {
            lock.unlock();
            continue;
        }

        VkCommandBuffer cmd = get_command_buffer(false);
        VkFence fence{};
        VkFenceCreateInfo fence_info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VK_CHECK(vkCreateFence(renderer->dev, &fence_info, {}, &fence));
        // NOTE: maybe these should be made static to avoid constant alloc/dealloc of memory (or completely removed)
        std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> wait_sems;
        std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signal_sems;
        std::map<std::pair<VkBuffer, VkBuffer>, std::vector<VkBufferCopy>> buffer_regions;
        for(const auto& e : uploads) {
            bool is_bulk_final = false;

            if(e.is_final) {
                const auto ct = e.t->completed_transactions->fetch_add(1) + 1;
                is_bulk_final = ct == e.t->transactions_in_bulk;
                if(is_bulk_final) {
                    if(e.t->signal_semaphore) {
                        signal_sems.emplace_back(e.t->signal_semaphore, VK_PIPELINE_STAGE_TRANSFER_BIT);
                    }
                }
            }

            if(e.t->dst_type == BUFFER) {
                std::pair<VkBuffer, VkBuffer> dst_src;
                if(e.is_src_allocation()) {
                    dst_src = std::make_pair(e.t->dst.buffer, pool_memory->buffer);
                } else if(e.is_src_vkbuffer()) {
                    dst_src = std::make_pair(e.t->dst.buffer, std::get<1>(e.src_storage));
                }
                buffer_regions[dst_src].push_back(e.copy_region.buffer);
            } else if(e.t->dst_type == IMAGE) {
                if(!e.is_src_allocation()) {
                    assert(false);
                    continue;
                }
                vkCmdCopyBufferToImage(cmd, pool_memory->buffer, e.t->dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1, &e.copy_region.image);
                if(e.is_final) {
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
                if(e.t->wait_on_dst_acq_sem) {
                    e.t->wait_on_dst_acq_sem = false;
                    wait_sems.emplace_back(e.t->image_dst_acq_sem, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
                }
            }
        }
        for(const auto& [dst_src, regs] : buffer_regions) {
            vkCmdCopyBuffer(cmd, dst_src.second, dst_src.first, regs.size(), regs.data());
        }
        cmdpool->end(cmd);
        submit_queue->enqueue({ .buffers = { cmd }, .waits = wait_sems, .signals = signal_sems }, fence);
        lock.unlock();
        using namespace std::chrono_literals;
        VK_CHECK(vkWaitForFences(renderer->dev, 1, &fence, true,
                                 std::chrono::duration_cast<std::chrono::nanoseconds>(100ms).count()));
        lock.lock();
        for(const auto& e : uploads) {
            const auto s = e.get_size();
            e.t->uploaded += s;
        }
        for(auto it = transactions.before_begin(); it != transactions.end();) {
            if(auto n = std::next(it); n != transactions.end() && n->get_remaining() == 0) {
                if((n->completed_transactions->load()) == n->transactions_in_bulk) {
                    if(n->image_dst_acq_sem) { renderer->destroy_semaphore(n->image_dst_acq_sem); }
                    if(n->on_complete_flag) {
                        n->on_complete_flag->test_and_set();
                        n->on_complete_flag->notify_all();
                    }
                }
                transactions.erase_after(it);
            } else {
                ++it;
            }
        }
        vkDestroyFence(renderer->dev, fence, nullptr);
        pool->free();
        lock.unlock();
    }
}

VkCommandBuffer GpuStagingManager::get_command_buffer(bool lock) {
    ++allocated_command_buffers;
    if(lock) {
        std::scoped_lock _lock{ queue_mutex };
        return cmdpool->begin_onetime();
    }
    return cmdpool->begin_onetime();
}

constexpr size_t GpuStagingManager::Transaction::get_remaining() const { return get_size() - uploaded; }

constexpr size_t GpuStagingManager::Transaction::get_size() const { return upload_size; }

constexpr size_t GpuStagingManager::Upload::get_size() const {
    if(is_src_allocation()) {
        return std::get<0>(src_storage).second;
    } else if(is_src_vkbuffer()) {
        return copy_region.buffer.size;
    } else {
        assert(false);
        return 0;
    }
}
