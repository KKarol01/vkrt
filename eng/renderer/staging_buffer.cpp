#include "staging_buffer.hpp"
#include <eng/renderer/renderer.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/vulkan/vulkan_backend.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan/vulkan_structs.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <eng/renderer/vulkan/to_vk.hpp>
#include <vulkan/vulkan_core.h>

namespace eng
{
namespace gfx
{

void StagingBuffer::init(SubmitQueue* queue)
{
    auto& r = get_renderer();
    ENG_ASSERT(queue != nullptr);

    const auto alloc_size = align_up2(CAPACITY, ALIGNMENT);
    buffer = Buffer::init(alloc_size, BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT | BufferUsage::CPU_ACCESS);
    r.backend->allocate_buffer(buffer);
    r.backend->set_debug_name(buffer, "staging buffer");
    ENG_ASSERT(buffer.memory);

    this->queue = queue;

    contexts.resize(r.frame_delay);
    for(auto i = 0u; i < r.frame_delay; ++i)
    {
        contexts[i].pool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    }
    sync = r.make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0ull, "staging sync" });
}

Sync* StagingBuffer::flush(Sync* signal_sync)
{
    if(!get_context().cmd) { return sync; } // no transactions issued since previous flush
    get_context().pool->end(get_context().cmd);
    queue->with_cmd_buf(get_context().cmd).signal_sync(sync, PipelineStage::TRANSFER_BIT);
    if(signal_sync != nullptr) { queue->signal_sync(signal_sync, PipelineStage::TRANSFER_BIT); }
    queue->submit();
    get_context().cmd = nullptr;
    return sync;
}

void StagingBuffer::reset()
{
    flush(nullptr); // send, if any, staged transactions before optional forwarding.
    sync->wait_cpu(~0ull);
    // sync->reset();
    get_context().pool->reset();
    get_context().cmd = nullptr;
    head = 0;
}

void StagingBuffer::copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range64u src_range, bool insert_barrier)
{
    if(src_range.size == 0) { return; }
    if(!translate_dst_offset(dst, dst_offset, src_range.size)) { return; }
    if(dst.memory != nullptr && src.memory != nullptr)
    {
        memcpy((std::byte*)dst.memory + dst_offset, (const std::byte*)src.memory + src_range.offset, src_range.size);
    }
    else
    {
        if(insert_barrier) { barrier(); }
        get_cmd()->copy(dst, src, dst_offset, src_range);
    }
    dst.size = std::max(dst.size, dst_offset + src_range.size);
}

void StagingBuffer::copy(Buffer& dst, const void* const src, size_t dst_offset, size_t src_size, bool insert_barrier)
{
    if(src_size == 0) { return; }
    ENG_ASSERT(src);
    if(!translate_dst_offset(dst, dst_offset, src_size)) { return; }

    if(dst.memory) { memcpy((std::byte*)dst.memory + dst_offset, (const std::byte*)src, src_size); }
    else
    {
        size_t uploaded = 0;
        while(uploaded < src_size)
        {
            const auto upload_size = src_size - uploaded;
            auto alloc = partial_allocate(upload_size);
            memcpy(get_alloc_mem(alloc), (const std::byte*)src + uploaded, alloc.size);
            get_cmd()->copy(dst, buffer, dst_offset + uploaded, Range64u{ alloc.offset, alloc.size });
            uploaded += alloc.size;
        }
        ENG_ASSERT(uploaded == src_size);
        if(insert_barrier) { barrier(); }
    }
    dst.size = std::max(dst.size, dst_offset + src_size);
}

void StagingBuffer::copy(Image& dst, const Image& src, const ImageCopy& copy, bool transition_back)
{
    const auto dstrange = ImageMipLayerRange{ .mips = { copy.dstlayers.mip, 1 }, .layers = copy.dstlayers.layers };
    const auto srcrange = ImageMipLayerRange{ .mips = { copy.srclayers.mip, 1 }, .layers = copy.srclayers.layers };
    prepare_image(&dst, &src, true, false, dstrange, srcrange);
    get_cmd()->copy(dst, src, copy);
    if(transition_back) { prepare_image(&dst, &src, false, true, dstrange, srcrange); }
    else { barrier(); }
}

void StagingBuffer::blit(Image& dst, const Image& src, const ImageBlit& blit, bool transition_back)
{
    const auto dstrange = ImageMipLayerRange{ .mips = { blit.dstlayers.mip, 1 }, .layers = blit.dstlayers.layers };
    const auto srcrange = ImageMipLayerRange{ .mips = { blit.srclayers.mip, 1 }, .layers = blit.srclayers.layers };
    prepare_image(&dst, &src, true, false, dstrange, srcrange);
    get_cmd()->blit(dst, src, blit);
    if(transition_back) { prepare_image(&dst, &src, false, true, dstrange, srcrange); }
    else { barrier(); }
}

size_t StagingBuffer::copy(Image& dst, const void* const src, uint32_t layer, uint32_t mip, bool transition_back,
                           DiscardContents discard, Vec3i32 offset, Vec3u32 extent)
{
    ENG_ASSERT(src);
    ENG_ASSERT(offset.z == 0);
    ENG_ASSERT(offset.x >= 0 && offset.y >= 0 && offset.z >= 0);

    const glm::u32vec3 mip_texels = { std::max(dst.width >> mip, 1u), std::max(dst.height >> mip, 1u),
                                      std::max(dst.depth >> mip, 1u) };

    const auto block_data = get_block_data(dst.format);
    if(extent.x == ~0u) { extent.x = dst.width; }
    if(extent.y == ~0u) { extent.y = dst.height; }
    if(extent.z == ~0u) { extent.z = dst.depth; }
    ENG_ASSERT(extent.z == 1u);

    const glm::u32vec3 blocks =
        glm::u32vec3{ extent.x, extent.y, extent.z } /
        glm::u32vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
    const auto block_count = blocks.x * blocks.y * blocks.z;
    const auto row_bytes = blocks.x * block_data.bytes_per_texel;
    const auto src_bytes = block_count * block_data.bytes_per_texel;

    prepare_image(&dst, nullptr, discard == DiscardContents::YES, false, { { mip, 1 }, { layer, 1 } });

    size_t bytes_uploaded = 0;
    while(bytes_uploaded < src_bytes)
    {
        const auto remaining_bytes = src_bytes - bytes_uploaded;
        const auto rows_uploaded = bytes_uploaded / row_bytes;
        auto alloc = partial_allocate(remaining_bytes);
        if(alloc.size < row_bytes)
        {
            reset();
            continue;
        }

        const auto rows_allocated = alloc.size / row_bytes;
        const auto usable_bytes = rows_allocated * row_bytes; // alloc.size rounded down
        memcpy(get_alloc_mem(alloc), (std::byte*)src + bytes_uploaded, usable_bytes);

        const auto copy_offset =
            glm::i32vec3{ offset.x, offset.y + rows_uploaded, offset.z } *
            glm::i32vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
        const auto copy_extent =
            glm::u32vec3{ extent.x, rows_allocated, 1 } *
            glm::u32vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
        auto copy = vk::VkBufferImageCopy2{};
        copy.bufferOffset = alloc.offset;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource = { to_vk(get_aspect_from_format(dst.format)), mip, layer, 1 };
        copy.imageOffset = { copy_offset.x, copy_offset.y, copy_offset.z };
        copy.imageExtent = { copy_extent.x, copy_extent.y, copy_extent.z };

        get_cmd()->copy(dst, buffer, &copy, 1);

        bytes_uploaded += usable_bytes;
    }
    if(transition_back) { prepare_image(&dst, nullptr, false, true, { { mip, 1 }, { layer, 1 } }); }
    else { barrier(); }
    ENG_ASSERT(bytes_uploaded == src_bytes);
    return src_bytes;
}

void StagingBuffer::barrier()
{
    get_cmd()->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW);
}

void StagingBuffer::barrier(Image& dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageMipLayerRange& range)
{
    ENG_ASSERT(dst_layout != ImageLayout::UNDEFINED);
    get_cmd()->barrier(dst, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW, src_layout, dst_layout, range);
}

Sync* StagingBuffer::get_wait_sem(bool flush)
{
    if(flush) { this->flush(nullptr); }
    return sync;
}

StagingBuffer::Allocation StagingBuffer::partial_allocate(size_t size)
{
    const auto aligned_size = align_up2(size, ALIGNMENT);

    if(get_free_space() == 0) { reset(); }

    const auto free_space = get_free_space();
    ENG_ASSERT(free_space >= ALIGNMENT && free_space % ALIGNMENT == 0);
    const auto real_size = std::min(free_space, aligned_size);
    const auto alloc = Allocation{
        .offset = head, .size = std::min(size, real_size), .real_size = real_size, .signal_value = sync->get_next_signal_value()
    };
    head = alloc.offset + alloc.real_size;
    ENG_ASSERT(head % ALIGNMENT == 0 && head <= CAPACITY);
    return alloc;
}

StagingBuffer::Context& StagingBuffer::get_context()
{
    auto& ctx = contexts[get_renderer().current_frame % get_renderer().frame_delay];
    if(last_frame != get_renderer().current_frame)
    {
        last_frame = get_renderer().current_frame;
        ctx.pool->reset(); // can safely assume all the transactions during that frame must have had completed (render graph waits for the semaphore)
    }
    return ctx;
}

ICommandBuffer* StagingBuffer::get_cmd()
{
    auto& ctx = get_context();
    if(!ctx.cmd) { ctx.cmd = ctx.pool->begin(); }
    return ctx.cmd;
}

void* StagingBuffer::get_alloc_mem(const Allocation& alloc) const
{
    return (void*)((char*)buffer.memory + alloc.offset);
}

void StagingBuffer::prepare_image(const Image* dst, const Image* src, bool discard_dst, bool finished,
                                  ImageMipLayerRange dst_range, ImageMipLayerRange src_range)
{
    if(!finished)
    {
        if(dst != nullptr)
        {
            const auto old_layout = discard_dst ? ImageLayout::UNDEFINED : dst->layout;
            get_cmd()->barrier(*dst, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                               PipelineAccess::TRANSFER_RW, old_layout, ImageLayout::TRANSFER_DST, dst_range);
        }
        if(src != nullptr)
        {
            get_cmd()->barrier(*src, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                               PipelineAccess::TRANSFER_RW, src->layout, ImageLayout::TRANSFER_SRC, src_range);
        }
    }
    else
    {
        if(dst != nullptr)
        {
            get_cmd()->barrier(*dst, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::ALL,
                               PipelineAccess::NONE, ImageLayout::TRANSFER_DST, dst->layout, dst_range);
        }
        if(src != nullptr)
        {
            get_cmd()->barrier(*src, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::ALL,
                               PipelineAccess::NONE, ImageLayout::TRANSFER_SRC, src->layout, src_range);
        }
    }
}

bool StagingBuffer::translate_dst_offset(Buffer& dst, size_t& offset, size_t size)
{
    if(offset == STAGING_APPEND) { offset = dst.size; }
    if(dst.capacity < offset + size)
    {
        ENG_ERROR("Buffer too small");
        return false;
    }
    return true;
}

#ifdef ENG_SBUF_DEBUG_STATS
void StagingBuffer::DebugStats::reset()
{
    ENG_LOG("[SBUF STATS]\ntc: {}, cmd: {}, sync: {}, bres: {}, fc: {}, lin: {}, free: {}", transaction_count,
            cmd_count, sync_count, buf_resize_count, flush_count, linalloc_count, freealloc_count);
    flush_count = 0;
    linalloc_count = 0;
    freealloc_count = 0;
    buf_resize_count = 0;
    transaction_count = 0;
    sync_count = 0;
    cmd_count = 0;
}
#endif

} // namespace gfx
} // namespace eng
