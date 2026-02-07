#include "staging_buffer.hpp"
#include <eng/renderer/renderer.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/to_vk.hpp>

namespace eng
{
namespace gfx
{

void StagingBuffer::init(SubmitQueue* queue)
{
    auto& r = get_renderer();
    ENG_ASSERT(queue != nullptr);

    const auto alloc_size = align_up2(CAPACITY, ALIGNMENT);
    buffer = r.make_buffer(Buffer::init("staging buffer", alloc_size,
                                        BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT | BufferUsage::CPU_ACCESS));
    ENG_ASSERT(buffer);
    memory = buffer->memory;

    ENG_ASSERT(memory != nullptr);
    this->queue = queue;
    allocator = DoubleSidedAllocator{ memory, CAPACITY };

    int alloc_dirs[]{ 1, -1 };
    for(int i = 0; i < 2; ++i)
    {
        dbs[i].dir = alloc_dirs[i];
        dbs[i].cmdpool =
            queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        ENG_ASSERT(dbs[i].cmdpool != nullptr);
    }
    db = &dbs[frame];
}

void StagingBuffer::flush(Sync* sync)
{
    if(!db->cmd) { return; } // no transactions issued since previous flush
    db->cmdpool->end(db->cmd);
    auto* submission = get_sync();
    queue->with_cmd_buf(db->cmd).signal_sync(submission, PipelineStage::TRANSFER_BIT);
    if(sync != nullptr) { queue->signal_sync(sync, PipelineStage::TRANSFER_BIT); }
    queue->submit();
    db->cmd = nullptr;
    db->submissions.push_back(submission);
}

void StagingBuffer::reset()
{
    flush(nullptr); // send, if any, staged transactions before optional forwarding.
    for(auto* e : db->submissions)
    {
        e->wait_cpu(~0ull);
        e->reset();
        syncs.push_back(e);
    }
    db->submissions.clear();
    db->cmdpool->reset();
    db->cmd = nullptr;
    allocator.reset(db->dir);
}

void StagingBuffer::next()
{
    flush(nullptr);
    frame = (frame + 1) & 0x1;
    db = &dbs[frame];
}

void StagingBuffer::queue_wait(SubmitQueue* q, Flags<PipelineStage> stage, bool flush)
{
    ENG_ASSERT(q != nullptr && stage);
    if(flush) { this->flush(nullptr); }
    for(auto* s : db->submissions)
    {
        q->wait_sync(s, stage);
    }
}

void StagingBuffer::copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range64u src_range, bool insert_barrier)
{
    ENG_ASSERT(dst && src);
    if(src_range.size == 0) { return; }
    if(!translate_dst_offset(dst, dst_offset, src_range.size)) { return; }
    auto& dstb = dst.get();
    const auto& srcb = src.get();
    if(dstb.memory != nullptr && srcb.memory != nullptr)
    {
        memcpy((std::byte*)dstb.memory + dst_offset, (const std::byte*)srcb.memory + src_range.offset, src_range.size);
    }
    else
    {
        get_cmd()->copy(dstb, srcb, dst_offset, src_range);
        if(insert_barrier) { barrier(); }
    }
    dstb.size = std::max(dstb.size, dst_offset + src_range.size);
}

void StagingBuffer::copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size, bool insert_barrier)
{
    if(src_size == 0) { return; }
    ENG_ASSERT(dst && src);
    if(!translate_dst_offset(dst, dst_offset, src_size)) { return; }

    auto& dstb = dst.get();
    if(dstb.memory) { memcpy((std::byte*)dstb.memory + dst_offset, (const std::byte*)src, src_size); }
    else
    {
        size_t uploaded = 0;
        while(uploaded < src_size)
        {
            const auto upload_size = src_size - uploaded;
            auto alloc = partial_allocate(upload_size);
            memcpy(alloc.mem, (const std::byte*)src + uploaded, alloc.size);
            get_cmd()->copy(dstb, buffer.get(), dst_offset + uploaded, Range64u{ alloc.offset, alloc.size });
            uploaded += alloc.size;
        }
        ENG_ASSERT(uploaded == src_size);
        if(insert_barrier) { barrier(); }
    }
    dstb.size = std::max(dstb.size, dst_offset + src_size);
}

void StagingBuffer::copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy, bool transition_back)
{
    ENG_ASSERT(dst && src);
    auto& dsti = dst.get();
    const auto& srci = src.get();
    const auto dstrange = ImageMipLayerRange{ .mips = { copy.dstlayers.mip, 1 }, .layers = copy.dstlayers.layers };
    const auto srcrange = ImageMipLayerRange{ .mips = { copy.srclayers.mip, 1 }, .layers = copy.srclayers.layers };
    prepare_image(&dsti, &srci, true, false, dstrange, srcrange);
    get_cmd()->copy(dsti, srci, copy);
    if(transition_back) { prepare_image(&dsti, &srci, false, true, dstrange, srcrange); }
    else { barrier(); }
}

void StagingBuffer::blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit, bool transition_back)
{
    ENG_ASSERT(dst && src);
    auto& dsti = dst.get();
    const auto& srci = src.get();
    const auto dstrange = ImageMipLayerRange{ .mips = { blit.dstlayers.mip, 1 }, .layers = blit.dstlayers.layers };
    const auto srcrange = ImageMipLayerRange{ .mips = { blit.srclayers.mip, 1 }, .layers = blit.srclayers.layers };
    prepare_image(&dsti, &srci, true, false, dstrange, srcrange);
    get_cmd()->blit(dsti, srci, blit);
    if(transition_back) { prepare_image(&dsti, &srci, false, true, dstrange, srcrange); }
    else { barrier(); }
}

size_t StagingBuffer::copy(Handle<Image> dst, const void* const src, uint32_t layer, uint32_t mip, bool transition_back,
                           StagingDiscard discard, Vec3i32 offset, Vec3u32 extent)
{
    ENG_ASSERT(dst && src);
    ENG_ASSERT(offset.z == 0);
    ENG_ASSERT(offset.x >= 0 && offset.y >= 0 && offset.z >= 0);

    auto& img = dst.get();

    const glm::u32vec3 mip_texels = { std::max(img.width >> mip, 1u), std::max(img.height >> mip, 1u),
                                      std::max(img.depth >> mip, 1u) };

    const auto block_data = get_block_data(img.format);
    if(extent.x == ~0) { extent.x = img.width; }
    if(extent.y == ~0) { extent.y = img.height; }
    if(extent.z == ~0) { extent.z = img.depth; }
    ENG_ASSERT(extent.z == 1);

    const glm::u32vec3 blocks =
        glm::u32vec3{ extent.x, extent.y, extent.z } /
        glm::u32vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
    const auto block_count = blocks.x * blocks.y * blocks.z;
    const auto row_bytes = blocks.x * block_data.bytes_per_texel;
    const auto src_bytes = block_count * block_data.bytes_per_texel;

    prepare_image(&img, nullptr, discard == StagingDiscard::DO, false, { { mip, 1 }, { layer, 1 } });

    size_t bytes_uploaded = 0;
    while(bytes_uploaded < src_bytes)
    {
        const auto remaining_bytes = src_bytes - bytes_uploaded;
        const auto rows_uploaded = bytes_uploaded / row_bytes;
        auto alloc = partial_allocate(remaining_bytes);
        if(alloc.size < row_bytes)
        {
            reset_allocator();
            continue;
        }

        const auto rows_allocated = alloc.size / row_bytes;
        const auto usable_bytes = rows_allocated * row_bytes; // alloc.size rounded down
        memcpy(alloc.mem, (std::byte*)src + bytes_uploaded, usable_bytes);

        const auto copy_offset =
            glm::i32vec3{ offset.x, offset.y + rows_uploaded, offset.z } *
            glm::i32vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
        const auto copy_extent =
            glm::u32vec3{ extent.x, rows_allocated, 1 } *
            glm::u32vec3{ block_data.texel_extent.x, block_data.texel_extent.y, block_data.texel_extent.z };
        const auto copy = Vks(VkBufferImageCopy2{
            .bufferOffset = alloc.offset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { to_vk(get_aspect_from_format(img.format)), mip, layer, 1 },
            .imageOffset = { copy_offset.x, copy_offset.y, copy_offset.z },
            .imageExtent = { copy_extent.x, copy_extent.y, copy_extent.z },
        });
        get_cmd()->copy(img, buffer.get(), &copy, 1);

        bytes_uploaded += usable_bytes;
    }
    if(transition_back) { prepare_image(&img, nullptr, false, true, { { mip, 1 }, { layer, 1 } }); }
    else { barrier(); }
    ENG_ASSERT(bytes_uploaded == src_bytes);
    return src_bytes;
}

void StagingBuffer::barrier()
{
    get_cmd()->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW);
}

void StagingBuffer::barrier(Handle<Image> dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageMipLayerRange& range)
{
    ENG_ASSERT(dst && dst_layout != ImageLayout::UNDEFINED);
    get_cmd()->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW, src_layout, dst_layout, range);
}

StagingBuffer::Allocation StagingBuffer::partial_allocate(size_t size)
{
    auto free_space = allocator.get_free_space();
    if(free_space < ALIGNMENT) { reset_allocator(); }
    free_space = allocator.get_free_space();
    const auto alloc_size = std::min(align_up2(size, ALIGNMENT), free_space);
    ENG_ASSERT(alloc_size > 0 && alloc_size % ALIGNMENT == 0);
    auto* mem = allocator.alloc(alloc_size, db->dir);
    Allocation alloc{ mem, (uintptr_t)mem - (uintptr_t)memory, std::min(alloc_size, size) };
    ENG_ASSERT(mem != nullptr);
    return alloc;
}

ICommandBuffer* StagingBuffer::get_cmd()
{
    if(!db->cmd) { db->cmd = db->cmdpool->begin(); }
    return db->cmd;
}

Sync* StagingBuffer::get_sync()
{
    Sync* s;
    if(syncs.size())
    {
        s = syncs.back();
        syncs.pop_back();
    }
    else { s = Engine::get().renderer->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "staging sync" }); }
    return s;
}

void StagingBuffer::reset_allocator()
{
    reset();
    if(allocator.get_free_space() < ALIGNMENT)
    {
        // reset next frame, as it hogs up all the memory.
        next();
        reset();
    }
    ENG_ASSERT(allocator.get_free_space() >= ALIGNMENT);
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

bool StagingBuffer::translate_dst_offset(Handle<Buffer> dst, size_t& offset, size_t size)
{
    if(offset == STAGING_APPEND) { offset = dst->size; }
    if(dst->capacity < offset + size)
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
