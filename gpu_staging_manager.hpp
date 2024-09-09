#pragma once

#include <cstddef>
#include <vector>
#include <forward_list>
#include <queue>
#include <memory>
#include <latch>
#include <vulkan/vulkan.h>

class FreeListAllocator;
class Buffer;
class QueueScheduler;

class GpuStagingManager {
    struct Transaction {
        constexpr size_t get_remaining() const { return data.size() - uploaded; }

        std::shared_ptr<std::latch> on_upload_complete_latch;
        std::vector<std::byte> data;
        VkBuffer dst;
        size_t dst_offset;
        size_t uploaded;
    };
    struct Upload {
        Transaction* t;
        VkBufferCopy region;
        void* pool_alloc;
    };

  public:
    GpuStagingManager() = default;
    explicit GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes);
    ~GpuStagingManager() noexcept;

    GpuStagingManager(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager& operator=(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager(GpuStagingManager&& other) noexcept = delete;
    GpuStagingManager& operator=(GpuStagingManager&& other) noexcept = delete;

    std::shared_ptr<std::latch> send_to(VkBuffer dst_buffer, size_t dst_offset, const void* data, size_t size_bytes);
    bool empty() const { return transactions.empty(); }

  private:
    void schedule_upload();
    void submit_uploads();

    VkCommandPool cmdpool{};

    QueueScheduler* submit_queue;
    Buffer* pool_memory;
    FreeListAllocator* pool;
    std::forward_list<Transaction> transactions;
    std::queue<Transaction*> queue;
    std::vector<Upload> uploads;

    std::jthread stage_thread;
    std::stop_token stop_token;
    std::mutex queue_mutex;
    std::condition_variable thread_cvar;
    std::atomic_int allocated_command_buffers{ 0 };
    std::atomic_int background_task_count{ 0 };
};