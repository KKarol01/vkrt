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
struct Buffer;

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

static constexpr auto STAGING_APPEND = std::numeric_limits<std::uint64_t>::max();

class StagingBuffer
{
  public:
    struct Batch
    {
        friend class StagingBuffer;

        Batch& send(const BufferCopy& copy);
        Batch& send(const ImageCopy& copy);
        void submit();

      private:
        explicit Batch(StagingBuffer* sb) noexcept : sb(sb) { assert(sb); }

        void maybe_resize(Handle<Buffer> bh, Buffer& b, size_t nsz);

        StagingBuffer* sb;
        std::vector<BufferCopy> bcps;
        std::vector<ImageCopy> icps;
    };

    StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept;

    Batch create_batch() { return Batch{ this }; }
    void submit(Batch&& batch);

  private:
    SubmitQueue* queue{};
    CommandPool* cmdpool{};
    Buffer* staging_buffer{};
    std::unique_ptr<LinearAllocator> allocator{};
};

} // namespace gfx

#undef IS_POW2
