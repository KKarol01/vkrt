#pragma once

#include <eng/renderer/submit_queue.hpp>
#include <eng/common/handle.hpp>
#include <eng/renderer/resources/resources.hpp>
#include <vulkan/vulkan.h>
#include <span>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <deque>
#include <mutex>
#include <numeric>
#include <eng/common/types.hpp>

namespace gfx
{
struct Buffer;

struct BufferCopy
{
    BufferCopy(Handle<Buffer> dst, size_t dst_offset, Handle<Buffer> src, Range src_range) noexcept
        : src(src), dst(dst), src_range(src_range), dst_offset(dst_offset)
    {
    }
    template <typename T>
    BufferCopy(Handle<Buffer> dst, size_t dst_offset, const T& src) noexcept
        : dst(dst), src_range(0, sizeof(T)), dst_offset(dst_offset), data(std::as_bytes(std::span{ &src, 1 }))
    {
    }
    template <typename T>
    BufferCopy(Handle<Buffer> dst, size_t dst_offset, const std::vector<T>& src) noexcept
        : dst(dst), src_range(0, src.size() * sizeof(T)), dst_offset(dst_offset), data(std::as_bytes(std::span{ src }))
    {
    }
    Handle<Buffer> src;
    Handle<Buffer> dst;
    Range src_range;
    size_t dst_offset;
    std::span<const std::byte> data;
};

struct ImageCopy
{
    template <typename T>
    ImageCopy(Handle<Image> dst, VkImageLayout final_layout, std::span<const T> src) noexcept
        : dst(dst), final_layout(final_layout), data(std::as_bytes(src))
    {
    }
    template <typename T>
    ImageCopy(Handle<Image> dst, VkImageLayout final_layout, const std::vector<T>& src) noexcept
        : dst(dst), final_layout(final_layout), data(std::as_bytes(std::span{ src }))
    {
    }
    Handle<Image> dst;
    VkImageLayout final_layout;
    std::span<const std::byte> data;
};

static constexpr auto STAGING_APPEND = std::numeric_limits<std::size_t>::max();

class StagingBuffer
{
    static constexpr auto CAPACITY = 64ull * 1024 * 1024;
    static constexpr auto ALIGNMENT = 512u;

    struct Transaction
    {
        Handle<Buffer> dst_buffer() const;
        Handle<Image> dst_image() const;
        Handle<Buffer> src_buffer() const;
        Handle<Image> src_image() const;
        uint32_t dst_resource;
        uint32_t src_resource;
        size_t dst_offset;
        Range src_range;
        bool dst_is_buffer;
        bool src_is_buffer;
        const void* alloc{};
        VkImageLayout final_layout{};
    };

  public:
    StagingBuffer(SubmitQueue* queue) noexcept;
    void stage(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range src_range);
    void stage(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size);
    void stage(Handle<Buffer> dst, std::span<const std::byte> src, size_t dst_offset);
    template <typename T> void stage(Handle<Buffer> dst, const T& src, size_t dst_offset)
    {
        stage(dst, &src, dst_offset, sizeof(T));
    }
    template <typename T> void stage(Handle<Buffer> dst, const std::vector<T>& src, size_t dst_offset)
    {
        stage(dst, src.data(), dst_offset, src.size() * sizeof(T));
    }

    // broken for vectors
    // template <typename... Ts> void stage_many(Handle<Buffer> dst, size_t dst_offset, const Ts&... ts)
    //{
    //     const auto total_size = (sizeof(Ts) + ...);
    //     const auto offset = resize_buffer(dst, dst_offset, total_size);
    //     const auto prefix_sum = [] {
    //         auto arr = std::array<size_t, sizeof...(Ts)>{ sizeof(Ts)... };
    //         std::exclusive_scan(arr.begin(), arr.end(), arr.begin(), size_t{});
    //         return arr;
    //     }();
    //     size_t idx = 0;
    //     (..., stage(dst, &ts, offset + prefix_sum[idx++], sizeof(Ts)));
    // }

    void stage(Handle<Image> dst, std::span<const std::byte> src, VkImageLayout final_layout);
    void flush();

  private:
    void* try_allocate(size_t size);
    std::pair<void*, size_t> allocate(size_t size);
    void* head_to_ptr() const { return static_cast<std::byte*>(data) + head; }
    size_t get_free_space() const { return CAPACITY - head; }
    size_t align(size_t sz) const { return (sz + ALIGNMENT - 1) & -ALIGNMENT; }
    size_t resize_buffer(Handle<Buffer> hbuf, size_t dst_offset, size_t src_size);
    size_t calc_alloc_head(void* ptr) const { return (std::uintptr_t)ptr - (std::uintptr_t)data; }
    void begin_cmd_buffer();
    void record_mem_barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                            VkAccessFlags2 dst_access);
    void record_mem_barrier(std::span<const VkImageMemoryBarrier2> barriers);
    void record_replacement_buffers();
    void record_copy(Buffer& dst, Buffer& src, size_t dst_offset, Range src_range);
    void record_copy(Image& dst, size_t src_offset);
    VkBufferCopy2 create_copy(size_t dst_offset, Range src_range) const;
    VkImageMemoryBarrier2 create_layout_transition(const Image& img, VkImageLayout src_layout, VkImageLayout dst_layout,
                                                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) const;

    Buffer& get_buffer(Handle<Buffer> buffer);

    std::vector<Transaction> transactions;
    std::vector<std::pair<Handle<Buffer>, Buffer>> replacement_buffers;

    SubmitQueue* queue{};
    VkFence fence{};
    CommandPool* cmdpool{};
    VkCommandBuffer cmd{};
    Handle<Buffer> buffer{};
    void* data{};
    size_t head{};
};

} // namespace gfx