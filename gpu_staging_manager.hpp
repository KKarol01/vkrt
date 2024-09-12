#pragma once

#include <cstddef>
#include <vector>
#include <forward_list>
#include <queue>
#include <variant>
#include <vulkan/vulkan.h>

class FreeListAllocator;
class Buffer;
class Image;
class QueueScheduler;

class GpuStagingManager {
    struct Transaction {
        constexpr size_t get_remaining() const;
        constexpr size_t get_size() const;
        constexpr bool is_dst_vkbuffer() const { return dst.index() == 0; }
        constexpr bool is_dst_vkimage() const { return dst.index() == 1; }
        constexpr bool is_src_vector() const { return src.index() == 0; }
        constexpr bool is_src_vkbuffer() const { return src.index() == 1; }

        std::atomic_flag* flag{};
        std::variant<std::vector<std::byte>, const Buffer*> src;
        std::variant<VkBuffer, VkImage> dst;
        std::variant<size_t, VkOffset3D> dst_offset;
        size_t uploaded{};
        uint32_t image_block_size{};
        VkExtent3D image_extent{};
        VkImageSubresourceLayers image_subresource{};
        VkSemaphore image_src_release_sem{};
        VkSemaphore image_dst_acquire_sem{};
        bool wait_on_sem{ true };
    };
    struct Upload {
        constexpr bool is_src_allocation() const { return src_storage.index() == 0; }
        constexpr bool is_src_vkbuffer() const { return src_storage.index() == 1; }
        constexpr size_t get_size(const GpuStagingManager& mgr) const;

        Transaction* t;
        std::variant<VkBufferCopy, VkBufferImageCopy> region;
        std::variant<void*, const Buffer*> src_storage;
    };

  public:
    GpuStagingManager() = default;
    explicit GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes);
    ~GpuStagingManager() noexcept;

    GpuStagingManager(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager& operator=(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager(GpuStagingManager&& other) noexcept = delete;
    GpuStagingManager& operator=(GpuStagingManager&& other) noexcept = delete;

    bool send_to(VkBuffer dst, size_t dst_offset, std::span<const std::byte> src, std::atomic_flag* flag = nullptr);
    bool send_to(VkBuffer dst, size_t dst_offset, const Buffer* src, std::atomic_flag* flag = nullptr);
    bool send_to(Image* dst, VkOffset3D dst_offset, std::span<const std::byte> src, VkCommandBuffer src_cmd,
                 QueueScheduler* src_queue, uint32_t src_queue_idx, std::atomic_flag* flag = nullptr);
    bool empty() const { return transactions.empty(); }
    std::mutex& get_queue_mutex() { return queue_mutex; }
    VkCommandPool get_cmdpool() { return cmdpool; }

  private:
    bool send_to_impl(VkBuffer dst, size_t dst_offset, std::variant<std::vector<std::byte>, const Buffer*>&& src,
                      std::atomic_flag* flag = nullptr);
    bool send_to_impl(Image* dst, VkImageSubresourceLayers dst_subresource, VkOffset3D dst_offset, VkExtent3D dst_extent,
                      std::span<const std::byte> src, uint32_t src_row_length, uint32_t src_image_height, VkCommandBuffer src_cmd,
                      QueueScheduler* src_queue, uint32_t src_queue_idx, std::atomic_flag* flag = nullptr);
    void schedule_upload();
    void submit_uploads();

    VkCommandPool cmdpool{};
    QueueScheduler* submit_queue;
    uint32_t queue_idx;
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