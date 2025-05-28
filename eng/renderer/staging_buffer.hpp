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
        std::scoped_lock sl{ mutex };
        const size_t free = size - head;
        const size_t alloc_sz = free < padded_sz ? free : padded_sz;
        const size_t oldhead = head;
        if(free == 0) { return { nullptr, 0 }; }
        head += padded_sz;
        ++num_allocs;
        return { static_cast<void*>(static_cast<std::byte*>(buffer) + oldhead), alloc_sz };
    }

    void reset()
    {
        std::scoped_lock sl{ mutex };
        assert(num_allocs > 0);
        if(--num_allocs == 0) { head = 0; }
    }

    size_t get_alloc_offset(const void* const palloc) const
    {
        return reinterpret_cast<size_t>(palloc) - reinterpret_cast<size_t>(buffer);
    }

    void* buffer{};
    size_t size{};
    std::mutex mutex;
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

    template <typename T> void send_to(Handle<Buffer> dst, size_t dst_offset, const T& src);
    template <typename T> void send_to(Handle<Buffer> dst, size_t dst_offset, const std::vector<T>& src);
    void send_to(Handle<Buffer> dst, Handle<Buffer> src, Range src_range, size_t dst_offset);
    template <typename T>
    void send_to(Handle<Image> dst, VkImageLayout final_layout, const VkBufferImageCopy2 region, const std::vector<T>& src);

  private:
    void send_to(const Transfer& transfer);
    VkImageMemoryBarrier2 generate_image_barrier(Handle<Image> image, VkImageLayout layout, bool is_final_layout);
    void transition_image(Handle<Image> image, VkImageLayout layout, bool is_final_layout);
    void record_command(VkCommandBuffer cmd, const Transfer& transfer);

    SubmitQueue* queue{};
    CommandPool* cmdpool{};
    Buffer* staging_buffer{};
    std::unique_ptr<LinearAllocator> allocator{};
};

template <typename T> void StagingBuffer::send_to(Handle<Buffer> dst, size_t dst_offset, const T& src)
{
    if(!dst)
    {
        ENG_WARN("Destination resource is invalid ({}).", *dst);
        assert(false);
        return;
    }
    send_to(Transfer{ .dst_res = *dst,
                      .src_type = ResourceType::VECTOR,
                      .dst_type = ResourceType::BUFFER,
                      .data = &src,
                      .src_range = { 0, sizeof(T) },
                      .dst_range = { dst_offset, sizeof(T) } });
}

template <typename T> void StagingBuffer::send_to(Handle<Buffer> dst, size_t dst_offset, const std::vector<T>& src)
{
    if(!dst)
    {
        ENG_WARN("Destination resource is invalid ({}).", *dst);
        assert(false);
        return;
    }
    send_to(Transfer{
        .src_type = ResourceType::VECTOR,
        .dst_res = *dst,
        .dst_type = ResourceType::BUFFER,
        .dst_range = { dst_offset, src.size() * sizeof(T) },
        .data = src.data(),
    });
}

template <typename T>
void StagingBuffer::send_to(Handle<Image> dst, VkImageLayout final_layout, const VkBufferImageCopy2 region,
                            const std::vector<T>& ts)
{
    if(!dst)
    {
        ENG_WARN("Destination resource is invalid ({}).", *dst);
        assert(false);
        return;
    }
    send_to(Transfer{ .src_type = ResourceType::VECTOR,
                      .dst_res = *dst,
                      .dst_type = ResourceType::IMAGE,
                      .src_range = { 0, ts.size() * sizeof(T) },
                      .data = ts.data(),
                      .dst_final_layout = final_layout,
                      .dst_image_region = region });
}

void StagingBuffer::send_to(Handle<Buffer> dst, Handle<Buffer> src, Range src_range, size_t dst_offset)
{
    if(!dst)
    {
        ENG_WARN("Destination resource is invalid ({}).", *dst);
        assert(false);
        return;
    }
    send_to(Transfer{ .src_res = *src,
                      .src_type = !src ? ResourceType::STAGING : ResourceType::BUFFER,
                      .dst_res = *dst,
                      .dst_type = ResourceType::BUFFER,
                      .src_range = src_range,
                      .dst_range = { dst_offset, src_range.size } });
}

} // namespace gfx
