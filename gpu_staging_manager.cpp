#include "gpu_staging_manager.hpp"
#include "free_list_allocator.hpp"
#include "renderer_vulkan.hpp"

GpuStagingManager::GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes)
    : submit_queue{ new QueueScheduler{ queue } } {
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

std::shared_ptr<std::latch> GpuStagingManager::send_to(VkBuffer dst_buffer, size_t dst_offset, const void* data, size_t size_bytes) {
    if(size_bytes == 0) { return std::make_shared<std::latch>(0u); }

    std::shared_ptr<std::latch> on_complete_latch = std::make_shared<std::latch>(1u);
    std::scoped_lock lock{ queue_mutex };
    transactions.push_front(Transaction{
        .on_upload_complete_latch = on_complete_latch,
        .data = { static_cast<const std::byte*>(data), static_cast<const std::byte*>(data) + size_bytes },
        .dst = dst_buffer,
        .dst_offset = dst_offset,
        .uploaded = 0,
    });
    queue.push(&transactions.front());
    thread_cvar.notify_one();
    return on_complete_latch;
}

void GpuStagingManager::schedule_upload() {
    for(;;) {
        if(queue.empty()) { return; }

        Transaction& t = *queue.front();

        size_t remaining = t.get_remaining();
        size_t offset = 0;
        for(;;) {
            if(pool->get_total_free_memory() == 0) { return; }

            if(remaining == 0) {
                queue.pop();
                break;
            }

            const size_t fit = pool->try_get_best_fit_size(remaining);
            if(fit == 0) { return; }

            const size_t upload_size = std::min(fit, remaining);

            void* alloc = pool->allocate(upload_size);
            if(!alloc) {
                assert(false);
                return;
            }

            memcpy(alloc, t.data.data() + t.uploaded + offset, upload_size);

            uploads.push_back(Upload{
                .t = &t,
                .region = { .srcOffset = pool->get_offset_bytes(alloc), .dstOffset = t.dst_offset + t.uploaded + offset, .size = upload_size },
                .pool_alloc = alloc });
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

        if(allocated_command_buffers >= 1024) {
            while(background_task_count > 0) {
                background_task_count.wait(background_task_count);
            }
            VK_CHECK(vkResetCommandPool(renderer->dev, cmdpool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));
            allocated_command_buffers = 0;
        }

        schedule_upload();
        auto uploads = std::move(this->uploads);
        this->uploads.clear();

        lock.unlock();

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

        VkFence fence{};
        VkFenceCreateInfo fence_info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VK_CHECK(vkCreateFence(renderer->dev, &fence_info, {}, &fence));

        vks::CommandBufferBeginInfo begin_info;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));
        for(const auto& e : uploads) {
            vkCmdCopyBuffer(cmd, pool_memory->buffer, e.t->dst, 1, &e.region);
        }
        VK_CHECK(vkEndCommandBuffer(cmd));

        ++allocated_command_buffers;

        submit_queue->enqueue({ { cmd } }, fence);

        ++background_task_count;
        std::thread clear_thread{ [&mutex = this->queue_mutex, &pool = this->pool, &transactions = this->transactions,
                                   &thread_cvar = this->thread_cvar, &cmdpool = this->cmdpool,
                                   &background_task_count = this->background_task_count, uploads = std::move(uploads),
                                   cmd, fence, renderer = RendererVulkan::get()] {
            VK_CHECK(vkWaitForFences(
                renderer->dev, 1, &fence, true,
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{ 100 }).count()));

            {
                std::scoped_lock lock{ mutex };
                for(const auto& e : uploads) {
                    e.t->uploaded += pool->get_alloc_data_size(e.pool_alloc);
                    pool->deallocate(e.pool_alloc);
                }
                for(auto it = transactions.before_begin(); it != transactions.end(); ++it) {
                    if(auto n = std::next(it); n != transactions.end() && n->uploaded == n->data.size()) {
                        n->on_upload_complete_latch->count_down();
                        transactions.erase_after(it);
                    }
                }
            }
            --background_task_count;
            thread_cvar.notify_one();
        } };
        clear_thread.detach();
    }
}
