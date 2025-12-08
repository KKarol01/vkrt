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
    void init(SubmitQueue* queue, const Callback<void(Handle<Buffer>)>& on_buffer_resize = {});
    Sync* flush();
    void reset();

    Allocation allocate(size_t size);
    CommandBuffer* get_cmd();
    Sync* get_sync();

    void resize(Handle<Buffer> dst, size_t new_size);
    void copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range64u src_range);
    void copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size);
    template <typename T> void copy(Handle<Buffer> dst, const std::vector<T>& src, size_t dst_offset)
    {
        copy(dst, src.data(), dst_offset, src.size() * sizeof(T));
    }

    void copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy);
    void blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit);
    void copy(Handle<Image> dst, const void* const src);

    void barrier();
    void barrier(Handle<Image> dst, ImageLayout dst_layout);
    void barrier(Handle<Image> dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageSubRange& range = {});

    void flush_resizes();
    void reset_allocator();

    void translate_dst_offset(Handle<Buffer> dst, size_t& offset, size_t size)
    {
        if(offset == STAGING_APPEND) { offset = dst->size; }
        if(dst->capacity < offset + size) { resize(dst, offset + size); }
    }

    void prepare_image(Handle<Image> dst, Handle<Image> src)
    {
        if(dst->current_layout != ImageLayout::TRANSFER_DST) { barrier(dst, ImageLayout::TRANSFER_DST); }
        if(src && src->current_layout != ImageLayout::TRANSFER_SRC) { barrier(src, ImageLayout::TRANSFER_SRC); }
    }

    Handle<Buffer> buffer;
    void* memory{};
    SubmitQueue* queue{};
    Callback<void(Handle<Buffer>)> on_buffer_resize;

    DoubleSidedAllocator allocator;
    struct DoubleBuffered
    {
        CommandPool* cmdpool{};
        CommandBuffer* cmd{};
        Sync* sync{};
        int alloc_dir{};
    } _dbs[2]{};
    DoubleBuffered* dbs[2]{};
    DoubleBuffered* db{};
    std::vector<Sync*> syncs;

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