#pragma once

#include <cstddef>
#include <vector>
#include <forward_list>
#include <queue>
#include <vulkan/vulkan.h>

class FreeListAllocator;
class Buffer;

class GpuStagingManager {
    struct Transaction {
        constexpr size_t get_remaining() const { return data.size() - uploaded; }

        std::vector<std::byte> data;
        VkBuffer dst;
        size_t uploaded;
    };
    struct Upload {
        Transaction* t;
        VkBufferCopy region;
        void* pool_alloc;
    };

  public:
    GpuStagingManager() = default;
    explicit GpuStagingManager(size_t pool_size_bytes);
    ~GpuStagingManager() noexcept;

    GpuStagingManager(GpuStagingManager&& other) noexcept;
    GpuStagingManager& operator=(GpuStagingManager&& other) noexcept;

    void send_to(VkBuffer dst_buffer, size_t dst_offset, const void* data, size_t size_bytes);
    void update(VkCommandBuffer cmd);
    bool empty() const { return transactions.empty(); }

  private:
    void schedule_upload();
    void process_uploaded();
    void submit_uploads(VkCommandBuffer cmd);

    Buffer* pool_memory;
    FreeListAllocator* pool;
    std::forward_list<Transaction> transactions;
    std::queue<Transaction*> queue;
    std::vector<Upload> uploads;
};