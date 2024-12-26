#pragma once

#include <cstddef>
#include <vector>
#include <forward_list>
#include <queue>
#include <variant>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <vulkan/vulkan.h>

class LinearAllocator;
class Buffer;
class Image;
class QueueScheduler;
class CommandPool;

struct GpuStagingUpload {
    uint32_t src_queue_idx;
    std::variant<VkBuffer, Image*> dst;
    std::variant<VkBuffer, std::span<const std::byte>> src;
    std::variant<size_t, VkOffset3D> dst_offset{ 0ull };
    VkSemaphore dst_img_rel_sem{};
    size_t src_offset{};
    size_t size_bytes{};
};

class GpuStagingManager {
    enum ResourceType : uint8_t { BYTE_SPAN, BUFFER, IMAGE };
    struct Transaction {
        constexpr size_t get_remaining() const;
        constexpr size_t get_size() const;
        union DstUnion {
            DstUnion(const std::pair<std::byte*, size_t>& _byte_span) { byte_span = _byte_span; }
            DstUnion(const VkBuffer& _buffer) { buffer = _buffer; }
            DstUnion(const VkImage& _image) { image = _image; }
            std::pair<std::byte*, size_t> byte_span;
            VkBuffer buffer;
            VkImage image;
        } dst;
        union SrcUnion {
            SrcUnion(const std::pair<std::byte*, size_t>& _byte_span) { byte_span = _byte_span; }
            SrcUnion(const VkBuffer& _buffer) { buffer = _buffer; }
            std::pair<std::byte*, size_t> byte_span;
            VkBuffer buffer;
        } src;
        union DstOffsetUnion {
            DstOffsetUnion(size_t boffset) { buffer_offset = boffset; }
            DstOffsetUnion(VkOffset3D ioffset) { image_offset = ioffset; }
            size_t buffer_offset;
            VkOffset3D image_offset;
        } dst_offset;
        union {
            size_t buffer_offset;
        } src_offset;
        VkSemaphore image_dst_acq_sem{};
        VkSemaphore signal_semaphore{};
        std::atomic_flag* on_complete_flag{};
        std::shared_ptr<std::atomic_int> completed_transactions;
        uint32_t transactions_in_bulk{};
        size_t uploaded{};
        size_t upload_size{};
        uint32_t src_queue_idx;
        uint32_t image_block_size{};
        VkExtent3D image_extent{};
        VkImageSubresourceLayers image_subresource{};
        ResourceType dst_type;
        ResourceType src_type;
        bool wait_on_dst_acq_sem{ true };
    };
    struct Upload {
        constexpr bool is_src_allocation() const { return src_storage.index() == 0; }
        constexpr bool is_src_vkbuffer() const { return src_storage.index() == 1; }
        constexpr size_t get_size() const;
        Transaction* t;
        union {
            VkBufferCopy buffer;
            VkBufferImageCopy image;
        } copy_region;
        std::variant<std::pair<void*, size_t>, VkBuffer> src_storage;
        bool is_final{ false };
    };

  public:
    GpuStagingManager() = default;
    explicit GpuStagingManager(VkQueue queue, uint32_t queue_index, size_t pool_size_bytes);
    ~GpuStagingManager() noexcept;

    GpuStagingManager(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager& operator=(const GpuStagingManager&) noexcept = delete;
    GpuStagingManager(GpuStagingManager&& other) noexcept = delete;
    GpuStagingManager& operator=(GpuStagingManager&& other) noexcept = delete;

    bool send_to(std::span<const GpuStagingUpload> uploads, VkSemaphore wait_sem = nullptr,
                 VkSemaphore signal_sem = nullptr, std::atomic_flag* flag = nullptr);
    bool send_to(const GpuStagingUpload& uploads, VkSemaphore wait_sem = nullptr, VkSemaphore signal_sem = nullptr,
                 std::atomic_flag* flag = nullptr);
    bool empty() const { return transactions.empty(); }

  private:
    void schedule_upload();
    void submit_uploads();
    VkCommandBuffer get_command_buffer(bool lock);

    CommandPool* cmdpool{};
    QueueScheduler* submit_queue;
    uint32_t queue_idx;
    Buffer* pool_memory;
    LinearAllocator* pool;
    std::forward_list<Transaction> transactions;
    std::queue<Transaction*> queue;
    std::vector<Upload> uploads;

    std::jthread stage_thread;
    std::stop_token stop_token;
    std::mutex queue_mutex;
    std::condition_variable thread_cvar;
    std::atomic_int allocated_command_buffers{ 0 };
};