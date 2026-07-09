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

StagingBufferAllocatorRingBuffer::StagingBufferAllocatorRingBuffer(BufferView buffer, Sync* sync)
    : IStagingBufferAllocator(buffer, sync)
{
}

StagingBufferAllocation StagingBufferAllocatorRingBuffer::allocate_at_most(usize req_size, usize min_req_size)
{
    if(req_size == 0 || min_req_size > m_size || min_req_size > req_size) { return {}; }
    if(!m_allocs.empty() && m_sync->get_next_signal_value() == m_allocs.front().fence_value) { return {}; }

    reclaim_space_non_blocking();

    const usize aligned_req = align_up2(req_size, ALIGNMENT);

    usize available = get_contiguous_memory_available_from_offset(m_tail);

    while(available < min_req_size)
    {
        if(!m_is_full && m_head <= m_tail && min_req_size < (m_size - m_tail))
        {
            m_tail = 0;
            m_is_full = m_tail == m_head;
        }
        else { reclaim_space_blocking(); }
        available = get_contiguous_memory_available_from_offset(m_tail);
    }

    const usize actual_aligned = std::min(aligned_req, available);
    const usize alloc_offset = m_tail;

    m_tail += actual_aligned;
    if(m_tail == m_size) { m_tail = 0; }
    if(m_tail == m_head) { m_is_full = true; }

    const u64 fence_value = m_sync->get_next_signal_value();

    if(!m_allocs.empty() && m_allocs.back().fence_value == fence_value) { m_allocs.back().tail = m_tail; }
    else { m_allocs.push_back(InFlightAlloc{ .fence_value = fence_value, .tail = m_tail }); }

    return StagingBufferAllocation{
        .ptr = (u8*)m_ptr + alloc_offset,
        .offset = alloc_offset,
        .size = std::min(req_size, actual_aligned),
        .actual_size = actual_aligned,
    };
}

void StagingBufferAllocatorRingBuffer::reclaim_space_non_blocking()
{
    if(m_allocs.empty()) { return; }
    while(!m_allocs.empty() && m_sync->wait_cpu(0, m_allocs.front().fence_value))
    {
        m_head = m_allocs.front().tail;
        m_is_full = false;
        m_allocs.pop_front();
    }
    if(m_allocs.empty())
    {
        m_head = 0;
        m_tail = 0;
        m_is_full = false;
    }
}

void StagingBufferAllocatorRingBuffer::reclaim_space_blocking()
{
    ENG_ASSERT(!m_allocs.empty());
    m_sync->wait_cpu(~0ull, m_allocs.front().fence_value);
    reclaim_space_non_blocking();
}

usize StagingBufferAllocatorRingBuffer::get_contiguous_memory_available_from_offset(usize offset)
{
    if(m_is_full) { return 0; }
    if(offset < m_head) { return m_head - offset; }
    return m_size - offset;
}

void StagingBuffer::init(SubmitQueue* queue, IStagingBufferAllocator* allocator)
{
    auto& r = get_renderer();
    ENG_ASSERT(queue != nullptr && allocator != nullptr);
    m_queue = queue;
    m_allocator = allocator;
}

Sync* StagingBuffer::flush(bool signal_sync)
{
    Sync* out_signal_sync = m_allocator->m_sync;
    m_queue->signal_sync(out_signal_sync, PipelineStage::TRANSFER_BIT);
    if(m_cmd)
    {
        get_frame_data().cmdpool->end(m_cmd);
        m_queue->with_cmd_buf(m_cmd);
        signal_sync = true;
        m_cmd = nullptr;
    }
    if(signal_sync) { m_queue->submit(); }
    return out_signal_sync;
}

void StagingBuffer::flush_wait() { flush(true)->wait_cpu(~0ull); }

void StagingBuffer::reset()
{
    ENG_TIMER_SCOPED("Staging reset");
    flush_wait();
    m_allocator->reset();
}

void StagingBuffer::copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range64u src_range, bool insert_barrier)
{
    if(src_range.size == 0) { return; }
    if(!translate_dst_offset(dst, dst_offset, src_range.size)) { return; }
    if(dst.memory && src.memory)
    {
        memcpy((char*)dst.memory + dst_offset, (const char*)src.memory + src_range.offset, src_range.size);
        return;
    }
    if(insert_barrier) { barrier(); }
    get_cmd()->copy(dst, src, dst_offset, src_range);
    dst.size = std::max(dst.size, dst_offset + src_range.size);
}

void StagingBuffer::copy(Buffer& dst, const void* const src, size_t dst_offset, size_t src_size, bool insert_barrier)
{
    if(src_size == 0) { return; }
    ENG_ASSERT(src);
    if(!translate_dst_offset(dst, dst_offset, src_size)) { return; }
    if(dst.memory)
    {
        memcpy((char*)dst.memory + dst_offset, src, src_size);
        return;
    }
    size_t uploaded = 0;
    while(uploaded < src_size)
    {
        const auto upload_size = src_size - uploaded;
        auto alloc = m_allocator->allocate_at_most(upload_size);
        if(!alloc)
        {
            flush(false);
            continue;
        }
        memcpy(alloc.ptr, (const std::byte*)src + uploaded, alloc.size);
        get_cmd()->copy(dst, m_allocator->m_buffer.get(), dst_offset + uploaded, Range64u{ alloc.offset, alloc.size });
        uploaded += alloc.size;
    }
    ENG_ASSERT(uploaded == src_size);
    if(insert_barrier) { barrier(); }
    dst.size = std::max(dst.size, dst_offset + src_size);
}

void StagingBuffer::copy(Image& dst, const Image& src, const ImageCopy& copy, bool transition_back)
{
    const auto dstrange = ImageMipsLayers{ .mips = { copy.dstlayers.mip, 1 }, .layers = copy.dstlayers.layers };
    const auto srcrange = ImageMipsLayers{ .mips = { copy.srclayers.mip, 1 }, .layers = copy.srclayers.layers };
    prepare_image(&dst, &src, true, false, dstrange, srcrange);
    get_cmd()->copy(dst, src, copy);
    if(transition_back) { prepare_image(&dst, &src, false, true, dstrange, srcrange); }
    else { barrier(); }
}

void StagingBuffer::blit(Image& dst, const Image& src, const ImageBlit& blit, bool transition_back)
{
    const auto dstrange = ImageMipsLayers{ .mips = { blit.dstlayers.mip, 1 }, .layers = blit.dstlayers.layers };
    const auto srcrange = ImageMipsLayers{ .mips = { blit.srclayers.mip, 1 }, .layers = blit.srclayers.layers };
    prepare_image(&dst, &src, true, false, dstrange, srcrange);
    get_cmd()->blit(dst, src, blit);
    if(transition_back) { prepare_image(&dst, &src, false, true, dstrange, srcrange); }
    else { barrier(); }
}

size_t StagingBuffer::copy(Image& dst, const void* const src, u32 layer, u32 mip, bool transition_back,
                           DiscardContents discard, i32_3 offset, u32_3 extent)
{
    ENG_ASSERT(src);
    ENG_ASSERT(offset.z == 0);
    ENG_ASSERT(offset.x >= 0 && offset.y >= 0 && offset.z >= 0);

    const glm::u32vec3 mip_texels = { std::max(dst.width >> mip, 1u), std::max(dst.height >> mip, 1u),
                                      std::max(dst.depth >> mip, 1u) };

    const auto block_data = get_block_data(dst.format);
    if(extent.x == ~0u) { extent.x = mip_texels.x; }
    if(extent.y == ~0u) { extent.y = mip_texels.y; }
    if(extent.z == ~0u) { extent.z = mip_texels.z; }
    ENG_ASSERT(extent.x > 0);
    ENG_ASSERT(extent.y > 0);
    ENG_ASSERT(extent.z == 1);

    glm::u64vec3 blocks = glm::u64vec3{ extent.x, extent.y, extent.z } /
                          glm::u64vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
    const auto row_bytes = size_t(blocks.x) * size_t(block_data.bytes_per_texel);
    const auto src_bytes = size_t(blocks.x) * size_t(blocks.y) * size_t(blocks.z) * size_t(block_data.bytes_per_texel);

    prepare_image(&dst, nullptr, discard == DiscardContents::YES, false, { { mip, 1 }, { layer, 1 } });

    size_t bytes_uploaded = 0;
    while(bytes_uploaded < src_bytes)
    {
        const size_t remaining_bytes = src_bytes - bytes_uploaded;
        const size_t block_rows_uploaded = bytes_uploaded / row_bytes;
        auto alloc = m_allocator->allocate_at_most(remaining_bytes, row_bytes);
        if(!alloc)
        {
            flush(false);
            continue;
        }
        ENG_ASSERT(alloc.size >= row_bytes);

        const size_t block_rows_allocated = alloc.size / row_bytes;
        const size_t usable_bytes = block_rows_allocated * row_bytes;
        memcpy(alloc.ptr, (const std::byte*)src + bytes_uploaded, usable_bytes);
        const glm::i32vec3 copy_offset = { offset.x, offset.y + i32(block_rows_uploaded * block_data.texel_extent.y),
                                           offset.z };
        const u32 texel_rows_uploaded = block_rows_uploaded * block_data.texel_extent.y;
        const u32 texel_rows_allocated = block_rows_allocated * block_data.texel_extent.y;
        const u32 remaining_texel_rows = extent.y - texel_rows_uploaded;
        const u32 copy_rows = std::min(texel_rows_allocated, remaining_texel_rows);
        const glm::u32vec3 copy_extent = { extent.x, copy_rows, extent.z };

        vk::VkBufferImageCopy2 copy{};
        copy.bufferOffset = alloc.offset;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource = { to_vk(get_aspect_from_format(dst.format)), mip, layer, 1 };
        copy.imageOffset = { copy_offset.x, copy_offset.y, copy_offset.z };
        copy.imageExtent = { copy_extent.x, copy_extent.y, copy_extent.z };

        get_cmd()->copy(dst, m_allocator->m_buffer.get(), &copy, 1);

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

void StagingBuffer::barrier(Image& dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageMipsLayers& range)
{
    ENG_ASSERT(dst_layout != ImageLayout::UNDEFINED);
    get_cmd()->barrier(dst, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW, src_layout, dst_layout, range);
}

ICommandBuffer* StagingBuffer::get_cmd()
{
    if(!m_cmd) { m_cmd = get_frame_data().cmdpool->begin(); }
    return m_cmd;
}

void StagingBuffer::prepare_image(const Image* dst, const Image* src, bool discard_dst, bool finished,
                                  ImageMipsLayers dst_range, ImageMipsLayers src_range)
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

bool StagingBuffer::translate_dst_offset(const Buffer& dst, size_t& offset, size_t size)
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
