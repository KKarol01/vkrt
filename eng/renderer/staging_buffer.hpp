#pragma once

#include <eng/renderer/submit_queue.hpp>
#include <eng/common/handle.hpp>
#include <vulkan/vulkan.h>
#include <span>
#include <vector>
#include <variant>
#include <thread>
#include <atomic>
#include <memory>
#include <deque>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)
#include <eng/common/types.hpp>

#define IS_POW2(x) (x > 0 && !(x & (x - 1)))

namespace gfx
{

struct LinearAllocator
{
    inline static constexpr size_t ALIGNMENT = 512;

    LinearAllocator(void* buffer, size_t sz) : buffer(buffer), size(sz)
    {
        assert(buffer && IS_POW2(sz) && (sz % ALIGNMENT) == 0);
    }

    /* Allocates as much as it can and returns the allocation and it's size, aligned to ALIGNMENT. */
    std::pair<void*, size_t> allocate_best_fit(size_t sz)
    {
        const auto padded_sz = (sz + ALIGNMENT - 1) & -ALIGNMENT;
        size_t oldhead = head.load(std::memory_order_relaxed);
        while(true)
        {
            const size_t free = size - oldhead;
            const size_t alloc_sz = free < padded_sz ? free : padded_sz;
            const size_t newhead = oldhead + padded_sz;
            if(free == 0) { return { nullptr, 0 }; }
            num_allocs.fetch_add(1, std::memory_order_acq_rel);
            if(head.compare_exchange_weak(oldhead, newhead, std::memory_order_relaxed))
            {
                return { static_cast<void*>(static_cast<std::byte*>(buffer) + oldhead), alloc_sz };
            }
            else { num_allocs.fetch_sub(1, std::memory_order_relaxed); }
        }
    }

    void reset()
    {
        if(num_allocs.fetch_sub(1, std::memory_order_relaxed) == 1) { head.store(0, std::memory_order_relaxed); }
    }

    void* offset_buffer(size_t sz) const { return static_cast<void*>(static_cast<std::byte*>(buffer) + sz); }
    size_t get_alloc_offset(const void* const palloc) const
    {
        return reinterpret_cast<size_t>(palloc) - reinterpret_cast<size_t>(buffer);
    }

    void* buffer{};
    size_t size{};
    std::atomic<size_t> head{};
    std::atomic<size_t> num_allocs{};
};

class StagingBuffer
{
    enum class ResourceType : uint16_t
    {
        NONE,
        BUFFER,
        IMAGE,
        VECTOR,
        STAGING,
    };
    struct Transfer
    {
        Handle<Buffer> src_buf() const { return Handle<Buffer>{ src_res }; }
        Handle<Image> src_img() const { return Handle<Image>{ src_res }; }
        Handle<Buffer> dst_buf() const { return Handle<Buffer>{ dst_res }; }
        Handle<Image> dst_img() const { return Handle<Image>{ dst_res }; }
        uint32_t src_res{ ~0u };
        ResourceType src_type{};
        uint32_t dst_res{ ~0u };
        ResourceType dst_type{};
        Range src_range;
        Range dst_range;
        const void* data;
        VkImageLayout dst_final_layout;
        VkBufferImageCopy2 dst_image_region;
    };

  public:
    StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept;

    template <typename T> void send_to(Handle<Buffer> buffer, size_t offset, const T& t);
    template <typename T> void send_to(Handle<Buffer> buffer, size_t offset, const std::vector<T>& ts);
    void send_to(Handle<Buffer> dst, Handle<Buffer> src, Range src_range, size_t dst_offset);
    template <typename T>
    void send_to(Handle<Image> image, VkImageLayout final_layout, const VkBufferImageCopy2 region, const std::vector<T>& ts);

  private:
    void send_to(const Transfer& transfer);
    void resize(Handle<Buffer> buffer, size_t new_size);
    VkImageMemoryBarrier2 generate_image_barrier(Handle<Image> image, VkImageLayout layout, bool is_final_layout);
    void transition_image(Handle<Image> image, VkImageLayout layout, bool is_final_layout);
    void record_command(VkCommandBuffer cmd, const Transfer& transfer);

    SubmitQueue* queue{};
    CommandPool* cmdpool{};
    Buffer* staging_buffer{};
    std::unique_ptr<LinearAllocator> allocator{};
};

template <typename T> void StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, const T& t)
{
    send_to(Transfer{ .dst_res = *buffer,
                      .src_type = ResourceType::VECTOR,
                      .dst_type = ResourceType::BUFFER,
                      .data = &t,
                      .src_range = { 0, sizeof(T) },
                      .dst_range = { offset, sizeof(T) } });
}

template <typename T> void StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, const std::vector<T>& ts)
{
    send_to(Transfer{
        .src_type = ResourceType::VECTOR,
        .dst_res = *buffer,
        .dst_type = ResourceType::BUFFER,
        .dst_range = { offset, ts.size() * sizeof(T) },
        .data = ts.data(),
    });
}

template <typename T>
void StagingBuffer::send_to(Handle<Image> image, VkImageLayout final_layout, const VkBufferImageCopy2 region,
                            const std::vector<T>& ts)
{
    send_to(Transfer{ .src_type = ResourceType::VECTOR,
                      .dst_res = *image,
                      .dst_type = ResourceType::IMAGE,
                      .src_range = { 0, ts.size() * sizeof(T) },
                      .data = ts.data(),
                      .dst_final_layout = final_layout,
                      .dst_image_region = region });
}

void StagingBuffer::send_to(Handle<Buffer> dst, Handle<Buffer> src, Range src_range, size_t dst_offset)
{
    send_to(Transfer{ .src_res = *src,
                      .src_type = ResourceType::BUFFER,
                      .dst_res = *dst,
                      .dst_type = ResourceType::BUFFER,
                      .src_range = src_range,
                      .dst_range = { dst_offset, src_range.size } });
}

} // namespace gfx
