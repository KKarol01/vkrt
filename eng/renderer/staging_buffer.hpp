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
#include <mutex>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)
#include <eng/common/types.hpp>

#ifndef IS_POW2
#define IS_POW2(x) (x > 0 && !(x & (x - 1)))
#endif

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
        const size_t free = size - head;
        const size_t alloc_sz = free < padded_sz ? free : padded_sz;
        const size_t oldhead = head;
        if(free == 0) { return { nullptr, 0 }; }
        head += alloc_sz;
        ++num_allocs;
        return { static_cast<void*>(static_cast<std::byte*>(buffer) + oldhead), std::min(alloc_sz, sz) };
    }

    void reset()
    {
        assert(num_allocs > 0);
        if(--num_allocs == 0) { head = 0; }
    }

    size_t get_alloc_offset(const void* const palloc) const
    {
        return reinterpret_cast<size_t>(palloc) - reinterpret_cast<size_t>(buffer);
    }

    void* buffer{};
    size_t size{};
    size_t head{};
    size_t num_allocs{};
};

class StagingBuffer
{
    enum class ResourceType : uint16_t
    {
        NONE,
        BUFFER,
        IMAGE,
        VECTOR,
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
        VkBufferImageCopy2 dst_image_region; // this is currently unused.
    };

  public:
    StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept;

    template <typename T> void send_to(Handle<Buffer> dst, size_t dst_offset, const T& src);
    template <typename T> void send_to(Handle<Buffer> dst, size_t dst_offset, const std::vector<T>& src);
    void send_to(Handle<Buffer> dst, Handle<Buffer> src, Range src_range, size_t dst_offset);
    template <typename T> void send_to(Handle<Image> dst, VkImageLayout final_layout, const std::vector<T>& src);
    void submit();

  private:
    void send_to(const Transfer& transfer);
    void record_image_transition(VkCommandBuffer cmd, const Transfer& transfer, bool is_final_layout, VkImageLayout layout);
    void record_buffer_copy(VkCommandBuffer cmd, VkBuffer dst, VkBuffer src, Range src_range, size_t dst_offset);

    SubmitQueue* queue{};
    CommandPool* cmdpool{};
    Buffer* staging_buffer{};
    std::vector<Transfer> pending;
    std::unique_ptr<LinearAllocator> allocator{};
};

template <typename T> void StagingBuffer::send_to(Handle<Buffer> dst, size_t dst_offset, const T& src)
{
    send_to(Transfer{ .src_type = ResourceType::VECTOR,
                      .dst_res = *dst,
                      .dst_type = ResourceType::BUFFER,
                      .src_range = { 0, sizeof(T) },
                      .dst_range = { dst_offset, sizeof(T) },
                      .data = &src });
}

template <typename T> void StagingBuffer::send_to(Handle<Buffer> dst, size_t dst_offset, const std::vector<T>& src)
{
    send_to(Transfer{
        .src_type = ResourceType::VECTOR,
        .dst_res = *dst,
        .dst_type = ResourceType::BUFFER,
        .src_range = { 0, src.size() * sizeof(T) },
        .dst_range = { dst_offset, src.size() * sizeof(T) },
        .data = src.data(),
    });
}

template <typename T>
void StagingBuffer::send_to(Handle<Image> dst, VkImageLayout final_layout, const std::vector<T>& ts)
{
    send_to(Transfer{ .src_type = ResourceType::VECTOR,
                      .dst_res = *dst,
                      .dst_type = ResourceType::IMAGE,
                      .src_range = { 0, ts.size() * sizeof(T) },
                      .data = ts.data(),
                      .dst_final_layout = final_layout });
}

} // namespace gfx

#undef IS_POW2
