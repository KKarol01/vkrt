#pragma once

#include <unordered_map>
#include <set>
#include <map>
#include <vector>
#include <deque>
#include <memory>
#include <optional>
#include <variant>

#include <eng/common/callback.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

namespace eng
{
namespace gfx
{
inline static constexpr auto STAGING_APPEND = ~0ull;

struct StagingBufferAllocation
{
    constexpr operator bool() const { return ptr != nullptr; }
    void* ptr{};         // pointer to the start of the allocation
    usize offset{};      // offset from the start of the buffer's memory
    usize size{};        // usable user data size for ie. memcpy
    usize actual_size{}; // actual size of the block (block can be bigger than user memory for example)
};

class IStagingBufferAllocator
{
  public:
    IStagingBufferAllocator(BufferView buffer, Sync* sync)
        : m_buffer(buffer.buffer), m_ptr(buffer.buffer->memory), m_size(buffer.buffer->capacity), m_sync(sync)
    {

        ENG_ASSERT(m_ptr);
    }
    virtual ~IStagingBufferAllocator() noexcept = default;
    // May fail or return smaller size than requested. If fails or size smaller than requested, call reset, and try again.
    virtual StagingBufferAllocation allocate_at_most(usize req_size, usize min_req_size = 1) = 0;
    virtual void reset() = 0;

    inline static constexpr usize ALIGNMENT = 256;
    Handle<Buffer> m_buffer;
    void* m_ptr{};
    usize m_size{};
    Sync* m_sync{};
};

class StagingBufferAllocatorLinear : public IStagingBufferAllocator
{
  public:
    StagingBufferAllocatorLinear(BufferView buffer, Sync* sync);
    ~StagingBufferAllocatorLinear() override = default;

    StagingBufferAllocation allocate_at_most(usize req_size, usize min_req_size = 1) override;
    void reset() override { m_head = 0; }

    void reclaim_space_non_blocking();
    void reclaim_space_blocking();
    usize get_contiguous_memory_available_from_offset(usize offset);

    usize m_head{}; // gpu read pos
};

class StagingBufferAllocatorRingBuffer : public IStagingBufferAllocator
{
    struct InFlightAlloc
    {
        u64 fence_value{};
        usize tail{};
    };

  public:
    StagingBufferAllocatorRingBuffer(BufferView buffer, Sync* sync);
    ~StagingBufferAllocatorRingBuffer() override = default;

    StagingBufferAllocation allocate_at_most(usize req_size, usize min_req_size = 1) override;
    void reset() override { /* noop */ }

    void reclaim_space_non_blocking();
    void reclaim_space_blocking();
    usize get_contiguous_memory_available_from_offset(usize offset);

    usize m_head{}; // gpu read pos
    usize m_tail{}; // cpu write pos
    bool m_is_full{};
    std::deque<InFlightAlloc> m_allocs;
};

class StagingBuffer
{
    static constexpr size_t ALIGNMENT = 256ull;

  public:
    void init(SubmitQueue* queue, IStagingBufferAllocator* allocator);
    // Flushes pending transactions. Optionally returns signal sync.
    Sync* flush(bool signal_sync);
    // Flushes and waits on the cpu until completion.
    void flush_wait();
    void reset();
    // Copies data from src buffer to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    void copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range64u src_range, bool insert_barrier = false);
    // Copies data from src buffer to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    void copy(Buffer& dst, const void* const src, size_t dst_offset, size_t src_size, bool insert_barrier = false);
    // Copies data from src vector to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    template <typename T>
    void copy(Buffer& dst, const std::vector<T>& src, size_t dst_offset, bool insert_barrier = false)
    {
        copy(dst, src.data(), dst_offset, src.size() * sizeof(T), insert_barrier);
    }
    // Copies data from any type to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    template <typename T> void copy_value(Buffer& dst, const T& t, size_t dst_offset, bool insert_barrier = false)
    {
        copy(dst, &t, dst_offset, sizeof(T), insert_barrier);
    }

    // Copies data from src image to dst image. Transition_back restores layouts, and if false, just inserts a barrier.
    void copy(Image& dst, const Image& src, const ImageCopy& copy, bool transition_back = true);
    // Blits src image to dst image. Transition_back restores layouts, and if false, just inserts a barrier.
    void blit(Image& dst, const Image& src, const ImageBlit& blit, bool transition_back = true);
    // Copy from src to layer and mip in dst, given subrange by offset and extent.
    // Optionally discard the layer/mip before copying (saves layout transition).
    // Optionally transition back the layer/mip to original layout from transfer dst, if not, inserts RW barrier.
    size_t copy(Image& dst, const void* const src, u32 layer, u32 mip, bool transition_back = true,
                DiscardContents discard = DiscardContents::YES, i32_3 offset = {}, u32_3 extent = { ~0u, ~0u, ~0u });

    // Inserts rw->rw barrier.
    void barrier();
    // Inserts image layout barrier with optional range.
    void barrier(Image& dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageMipsLayers& range = {});

  private:
    // Get command buffer. If recently flushed, cmd is nullptr, so this function optionally begins a new one.
    ICommandBuffer* get_cmd();

    void prepare_image(const Image* dst, const Image* src, bool discard_dst, bool finished,
                       ImageMipsLayers dst_range = { { 0u, ~0u }, { 0u, ~0u } },
                       ImageMipsLayers src_range = { { 0u, ~0u }, { 0u, ~0u } });
    bool translate_dst_offset(const Buffer& dst, size_t& offset, size_t size);

    ICommandBuffer* m_cmd{};
    SubmitQueue* m_queue{};
    IStagingBufferAllocator* m_allocator{};
};
} // namespace gfx
} // namespace eng