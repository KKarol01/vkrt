#pragma once

#include <eng/renderer/renderer_fwd.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/common/types.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/common/doublesidedalloc.hpp>

#include <vulkan/vulkan_core.h>

#include <unordered_map>
#include <vector>
#include <deque>
#include <memory>
#include <optional>
#include <variant>

namespace eng
{
namespace gfx
{
static constexpr auto STAGING_APPEND = ~0ull;

class StagingBuffer
{
    static constexpr auto CAPACITY = 64ull * 1024 * 1024;
    static constexpr auto ALIGNMENT = 512ull;

    struct Allocation
    {
        void* mem{};
        size_t offset{};
        size_t size{}; // min of realsize and user-requested size
    };

  public:
    void init(SubmitQueue* queue);
    // Flushes pending transactions and optionally notifies supplied sync.
    void flush(Sync* sync);
    // Flushes pending transactions, waits for completions of all submissions, and resets frame's data.
    void reset();
    // Forward counter to the next frame without waiting.
    void next();
    // Makes queue wait on all submissions.
    void queue_wait(SubmitQueue* q, Flags<PipelineStage> stage, bool flush);

    // Copies data from src buffer to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    void copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range64u src_range, bool insert_barrier = false);
    // Copies data from src buffer to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    void copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size, bool insert_barrier = false);
    // Copies data from src vector to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    template <typename T>
    void copy(Handle<Buffer> dst, const std::vector<T>& src, size_t dst_offset, bool insert_barrier = false)
    {
        copy(dst, src.data(), dst_offset, src.size() * sizeof(T), insert_barrier);
    }
    // Copies data from any type to dst buffer. Adjusts the size. Use STAGING_APPEND to append data instead of calculating offsets manually.
    template <typename T>
    void copy_value(Handle<Buffer> dst, const T& t, size_t dst_offset, bool insert_barrier = false)
    {
        copy(dst, &t, dst_offset, sizeof(T), insert_barrier);
    }

    // Copies data from src image to dst image. Transition_back restores layouts, and if false, just inserts a barrier.
    void copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy, bool transition_back = true);
    // Blits src image to dst image. Transition_back restores layouts, and if false, just inserts a barrier.
    void blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit, bool transition_back = true);
    // Copy from src to layer and mip in dst, given subrange by offset and extent.
    // Optionally discard the layer/mip before copying (saves layout transition).
    // Optionally transition back the layer/mip to original layout from transfer dst, if not, inserts RW barrier.
    size_t copy(Handle<Image> dst, const void* const src, uint32_t layer, uint32_t mip, bool transition_back = true,
                bool discard = false, Vec3i32 offset = {}, Vec3u32 extent = { ~0u, ~0u, ~0u });

    // Inserts rw->rw barrier.
    void barrier();
    // Inserts image layout barrier with optional range.
    void barrier(Handle<Image> dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageMipLayerRange& range = {});

  private:
    // Always succeeds, but may not allocate entire size due to lack of space.
    Allocation partial_allocate(size_t size);
    // Get command buffer. If recently flushed, cmd is nullptr, so this function optionally begins a new one.
    ICommandBuffer* get_cmd();
    // Get sync with reuse.
    Sync* get_sync();

    // Resets the current frame. If still not enough memory, resets the other one too.
    void reset_allocator();
    void prepare_image(const Image* dst, const Image* src, bool discard_dst, bool finished,
                       ImageMipLayerRange dst_range = { { 0u, ~0u }, { 0u, ~0u } },
                       ImageMipLayerRange src_range = { { 0u, ~0u }, { 0u, ~0u } });
    bool translate_dst_offset(Handle<Buffer> dst, size_t& offset, size_t size);

    Handle<Buffer> buffer;
    void* memory{};
    SubmitQueue* queue{};

    DoubleSidedAllocator allocator;
    struct DoubleBuffered
    {
        CommandPoolVk* cmdpool{};
        ICommandBuffer* cmd{};
        int dir{};
        std::vector<Sync*> submissions;
    } dbs[2]{};
    DoubleBuffered* db{};
    std::vector<Sync*> syncs;
    uint32_t frame{};

#ifdef ENG_SBUF_DEBUG_STATS
    struct DebugStats
    {
        void reset();
        uint32_t flush_count{};
        uint32_t linalloc_count{};
        uint32_t freealloc_count{};
        uint32_t buf_resize_count{};
        uint32_t transaction_count{};
        uint32_t sync_count{};
        uint32_t cmd_count{};
    } debugstats;
#endif
};
} // namespace gfx
} // namespace eng