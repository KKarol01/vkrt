#pragma once

#include <eng/renderer/renderer_fwd.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/common/types.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/renderer.hpp>

#include <unordered_map>
#include <set>
#include <map>
#include <vector>
#include <deque>
#include <memory>
#include <optional>
#include <variant>

namespace eng
{
namespace gfx
{
inline static constexpr auto STAGING_APPEND = ~0ull;

enum class DiscardContents : uint8_t
{
    NO,
    YES,
};

class StagingBuffer
{
    static constexpr size_t CAPACITY = 64ull * 1024 * 1024;
    static constexpr size_t ALIGNMENT = 1024ull;

    struct FreeAllocation
    {
        bool operator<(const FreeAllocation& a) const { return std::tie(index, offset) < std::tie(a.index, a.offset); }
        uint32_t index{};
        size_t offset{};
        size_t size{};
    };

    struct Allocation
    {
        size_t offset{};         // offset from the start of the buffer's memory
        size_t size{};           // may be less_eq than real_size, if user requested size not multiple of alignment
        size_t real_size{};      // not min'd down to user req size, if alloc happened to be bigger
        Sync* semaphore{};       // signaled when completed
        uint64_t signal_value{}; // value to wait on
    };

    // double buffered context so each frame cmd pool can be reset and cmd bufs reused.
    struct Context
    {
        ICommandPool* pool{};
        ICommandBuffer* cmd{};
        Sync* semaphore{};
    };

  public:
    void init(SubmitQueue* queue);
    // Flushes pending transactions.
    void flush();
    // Flushes pending transactions and waits for completions of all submissions.
    // void reset();

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
    size_t copy(Image& dst, const void* const src, uint32_t layer, uint32_t mip, bool transition_back = true,
                DiscardContents discard = DiscardContents::YES, Vec3i32 offset = {}, Vec3u32 extent = { ~0u, ~0u, ~0u });

    // Inserts rw->rw barrier.
    void barrier();
    // Inserts image layout barrier with optional range.
    void barrier(Image& dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageMipLayerRange& range = {});

    // Gets the semaphore to wait on until all issued transactions have happened.
     Sync* get_wait_sem(bool flush = true);

  private:
    // Always succeeds, but may not allocate entire size due to lack of space.
    Allocation partial_allocate(size_t size);
    void scan_for_finished_allocations();
    void merge_free_allocations();
    // Get frame dependent context
    Context& get_context();
    // Get command buffer. If recently flushed, cmd is nullptr, so this function optionally begins a new one.
    ICommandBuffer* get_cmd();
    Sync* get_sem();

    void* get_alloc_mem(const Allocation& alloc) const;

    void prepare_image(const Image* dst, const Image* src, bool discard_dst, bool finished,
                       ImageMipLayerRange dst_range = { { 0u, ~0u }, { 0u, ~0u } },
                       ImageMipLayerRange src_range = { { 0u, ~0u }, { 0u, ~0u } });
    bool translate_dst_offset(const Buffer& dst, size_t& offset, size_t size);

    SubmitQueue* queue{};
    Buffer buffer;
    std::vector<Context> contexts;
    std::vector<Sync*> semaphores;
    std::deque<Allocation> allocations;
    std::set<FreeAllocation> free_list; // key is floor(log2) of alloc size. min key is log2(alignment), max key is log2(capacity)
    uint64_t last_frame{};
	Sync* common_sem{};
};
} // namespace gfx
} // namespace eng