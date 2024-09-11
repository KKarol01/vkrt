#pragma once

#include <cstddef>
#include <vector>
#include <forward_list>
#include <queue>
#include <variant>
#include <vulkan/vulkan.h>

class FreeListAllocator;
class Buffer;
class QueueScheduler;

class GpuStagingManager {
    struct Transaction {
        constexpr size_t get_remaining() const;
        constexpr size_t get_size() const;

        VkSemaphore semaphore{};
        std::atomic_flag* flag{};
        std::variant<std::vector<std::byte>, const Buffer*> data;
        VkBuffer dst;
        size_t dst_offset;
        size_t uploaded;
    };
    struct Upload {
        Transaction* t;
        VkBufferCopy region;
        std::variant<void*, const Buffer*> src;
    };

  public:
    GpuStagingManager() = default;
    explicit GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes);
    ~GpuStagingManager() noexcept;

    GpuStagingManager(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager& operator=(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager(GpuStagingManager&& other) noexcept = delete;
    GpuStagingManager& operator=(GpuStagingManager&& other) noexcept = delete;

    bool send_to(VkBuffer dst, size_t dst_offset, std::span<const std::byte> src, VkSemaphore semaphore = nullptr,
                 std::atomic_flag* flag = nullptr);
    bool send_to(VkBuffer dst, size_t dst_offset, const Buffer* src, VkSemaphore semaphore = nullptr, std::atomic_flag* flag = nullptr);
    bool empty() const { return transactions.empty(); }

  private:
    bool send_to_impl(VkBuffer dst, size_t dst_offset, std::variant<std::vector<std::byte>, const Buffer*>&& src,
                      VkSemaphore semaphore = nullptr, std::atomic_flag* flag = nullptr);
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