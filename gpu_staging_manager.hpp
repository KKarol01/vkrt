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
class CommandPool;

class GpuStagingManager {
    enum ResourceType : uint8_t { BYTE_SPAN, BUFFER, IMAGE };

    struct Transaction {
        constexpr size_t get_remaining() const;
        constexpr size_t get_size() const;

        std::atomic_flag* on_complete_flag{};
        union {
            std::pair<std::byte*, size_t> byte_span;
            VkBuffer buffer;
            VkImage image;
        } dst;
        union {
            std::pair<std::byte*, size_t> byte_span;
            VkBuffer buffer;
        } src;
        union {
            size_t buffer_offset;
            VkOffset3D image_offset;
        } dst_offset;
        union {
            size_t buffer_offset;
        } src_offset;
        size_t uploaded{};
        size_t upload_size{};
        uint32_t src_queue_idx;
        uint32_t image_block_size{};
        VkExtent3D image_extent{};
        VkImageSubresourceLayers image_subresource{};
        VkSemaphore image_dst_acquire_sem{};
        ResourceType dst_type;
        ResourceType src_type;
        bool wait_on_sem{ true };
    };
    struct Upload {
        constexpr bool is_src_allocation() const { return src_storage.index() == 0; }
        constexpr bool is_src_vkbuffer() const { return src_storage.index() == 1; }
        constexpr size_t get_size(const GpuStagingManager& mgr) const;
        Transaction* t;
        union {
            VkBufferCopy buffer;
            VkBufferImageCopy image;
        } copy_region;
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
    bool send_to(VkBuffer dst, size_t dst_offset, VkBuffer src, size_t src_offset, size_t size, std::atomic_flag* flag = nullptr);
    bool send_to(Image* dst, VkOffset3D dst_offset, std::span<const std::byte> src, VkSemaphore src_release_sem,
                 uint32_t src_queue_idx, std::atomic_flag* flag = nullptr);
    bool empty() const { return transactions.empty(); }

  private:
    bool send_to_impl(VkBuffer dst, size_t dst_offset, std::variant<std::span<const std::byte>, VkBuffer>&& src,
                      size_t src_offset, size_t size, std::atomic_flag* flag);
    bool send_to_impl(Image* dst_image, VkImageSubresourceLayers dst_subresource, VkOffset3D dst_offset, VkExtent3D dst_extent,
                      std::span<const std::byte> src, uint32_t src_row_length, uint32_t src_image_height,
                      VkSemaphore src_release_semaphore, uint32_t src_queue_idx, std::atomic_flag* flag);
    void schedule_upload();
    void submit_uploads();

    CommandPool* cmdpool{};
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
    std::mutex cmdpool_mutex;
    std::condition_variable thread_cvar;
    std::atomic_int allocated_command_buffers{ 0 };
    std::atomic_int background_task_count{ 0 };
};