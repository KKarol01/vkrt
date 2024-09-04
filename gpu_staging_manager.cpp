#include "gpu_staging_manager.hpp"
#include "free_list_allocator.hpp"
#include "renderer_vulkan.hpp"

GpuStagingManager::GpuStagingManager(size_t pool_size_bytes) {
    vks::BufferCreateInfo binfo;
    binfo.size = pool_size_bytes;
    binfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo vinfo{ .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO,
                                   .preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT };

    pool_memory = new Buffer{ "staging buffer", binfo, vinfo };
    pool = new FreeListAllocator{ pool_memory->mapped, pool_size_bytes };

    assert(pool && "Pool creation failure");
    if(pool) {
        const size_t biggest_size = pool->try_get_best_fit_size(pool_size_bytes);
        assert(biggest_size > 0);
        void* alloc = pool->allocate(biggest_size);
        assert(alloc);
        pool->deallocate(alloc);
    }
}

GpuStagingManager::~GpuStagingManager() noexcept {
    if(pool) {
        delete pool;
        pool = nullptr;
    }
    if(pool_memory) {
        delete pool_memory;
        pool_memory = nullptr;
    }
}

GpuStagingManager::GpuStagingManager(GpuStagingManager&& other) noexcept { *this = std::move(other); }

GpuStagingManager& GpuStagingManager::operator=(GpuStagingManager&& other) noexcept {
    pool_memory = std::exchange(other.pool_memory, nullptr);
    pool = std::exchange(other.pool, nullptr);
    transactions = std::move(other.transactions);
    queue = std::move(other.queue);
    uploads = std::move(other.uploads);
    return *this;
}

void GpuStagingManager::send_to(VkBuffer dst_buffer, size_t dst_offset, const void* data, size_t size_bytes) {
    transactions.push_front(Transaction{
        .data = { static_cast<const std::byte*>(data), static_cast<const std::byte*>(data) + size_bytes },
        .dst = dst_buffer,
        .uploaded = 0,
    });
    queue.push(&transactions.front());
}

void GpuStagingManager::update(VkCommandBuffer cmd) {
    if(!pool) {
        assert(false && "Pool shouldn't be nullptr when update is called");
        return;
    }

    process_uploaded();
    schedule_upload();
    submit_uploads(cmd);
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
                .region = { .srcOffset = pool->get_offset_bytes(alloc), .dstOffset = t.uploaded + offset, .size = upload_size },
                .pool_alloc = alloc });
            remaining -= upload_size;
            offset += upload_size;
        }
    }
}

void GpuStagingManager::process_uploaded() {
    for(const auto& e : uploads) {
        e.t->uploaded += pool->get_alloc_data_size(e.pool_alloc);
        pool->deallocate(e.pool_alloc);
    }

    for(auto it = transactions.before_begin(); it != transactions.end(); ++it) {
        if(auto n = std::next(it); n != transactions.end() && n->uploaded == n->data.size()) {
            transactions.erase_after(it);
        }
    }

    uploads.clear();
}

void GpuStagingManager::submit_uploads(VkCommandBuffer cmd) {
    for(const auto& e : uploads) {
        vkCmdCopyBuffer(cmd, pool_memory->buffer, e.t->dst, 1, &e.region);
    }
}
