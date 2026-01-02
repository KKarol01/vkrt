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

void StagingBuffer::resize(Handle<Buffer> dst, size_t new_size)
{
    assert(dst);
    auto& dstb = dst.get();
    assert(dstb.size <= dstb.capacity);
    if(new_size <= dstb.capacity)
    {
        dstb.size = std::min(new_size, dstb.size);
        return;
    }

    auto newbuf = eng::Engine::get().renderer->backend->make_buffer(BufferDescriptor{ dstb.name, new_size, dstb.usage });
    if(dstb.size > 0)
    {
        if(dstb.memory)
        {
            flush()->wait_cpu(~0ull);
            memcpy(newbuf.memory, dstb.memory, dstb.size);
        }
        else
        {
            queue->wait_sync(flush(), PipelineStage::TRANSFER_BIT);
            get_cmd()->copy(newbuf, dstb, 0, { 0, dstb.size });
            flush()->wait_cpu(~0ull);
        }
    }

    newbuf.size = dstb.size;
    eng::Engine::get().renderer->backend->destroy_buffer(dstb);
    dstb = newbuf;
    if(on_buffer_resize) { on_buffer_resize(dst); }
}

void StagingBuffer::copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range64u src_range)
{
    assert(dst && src);
    if(src_range.size == 0) { return; }
    translate_dst_offset(dst, dst_offset, src_range.size);
    auto& dstb = dst.get();
    const auto& srcb = src.get();
    if(dstb.memory != nullptr && srcb.memory != nullptr)
    {
        memcpy((std::byte*)dstb.memory + dst_offset, (const std::byte*)srcb.memory + src_range.offset, src_range.size);
    }
    else { get_cmd()->copy(dstb, srcb, dst_offset, src_range); }
    dstb.size = std::max(dstb.size, dst_offset + src_range.size);
}

void StagingBuffer::copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size)
{
    assert(dst && src);
    if(src_size == 0) { return; }
    translate_dst_offset(dst, dst_offset, src_size);

    auto& dstb = dst.get();
    if(dstb.memory) { memcpy((std::byte*)dstb.memory + dst_offset, (const std::byte*)src, src_size); }
    else
    {
        size_t uploaded = 0;
        while(uploaded < src_size)
        {
            const auto upload_size = src_size - uploaded;
            auto alloc = allocate(upload_size);
            assert(alloc.mem != nullptr);
            memcpy(alloc.mem, (const std::byte*)src + uploaded, alloc.size);
            get_cmd()->copy(dstb, buffer.get(), dst_offset + uploaded, Range64u{ alloc.offset, alloc.size });
            uploaded += alloc.size;
        }
        assert(uploaded == src_size);
    }
    dstb.size = std::max(dstb.size, dst_offset + src_size);
}

void StagingBuffer::copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy)
{
    assert(dst && src);
    // prepare_image(dst, src);
    auto& dsti = dst.get();
    const auto& srci = src.get();
    // assert(srci.current_layout == ImageLayout::TRANSFER_SRC);
    // assert(dsti.current_layout == ImageLayout::TRANSFER_DST);
    const auto dstrange = ImageSubRange{ .mips = { copy.dstlayers.mip, 1 }, .layers = copy.dstlayers.layers };
    const auto srcrange = ImageSubRange{ .mips = { copy.srclayers.mip, 1 }, .layers = copy.srclayers.layers };
    get_cmd()->copy(dsti, srci, copy, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC);
}

void StagingBuffer::blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit)
{
    assert(dst && src);
    // prepare_image(dst, src);
    auto& dsti = dst.get();
    const auto& srci = src.get();
    const auto dstrange = ImageSubRange{ .mips = { blit.dstlayers.mip, 1 }, .layers = blit.dstlayers.layers };
    const auto srcrange = ImageSubRange{ .mips = { blit.srclayers.mip, 1 }, .layers = blit.srclayers.layers };
    get_cmd()->blit(dsti, srci, blit, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC, ImageFilter::LINEAR);
}

void StagingBuffer::copy(Handle<Image> dst, const void* const src)
{
    assert(dst && src);
    prepare_image(dst, {});
    auto& img = dst.get();
    const auto block_data = get_block_data(img.format);
    const auto bx = img.width / block_data.x;
    const auto by = img.height / block_data.y;
    const auto bz = img.depth / block_data.z;
    const auto bcount = bx * by * bz;
    const auto rowsize = bx * block_data.size;
    const auto imgsize = bcount * block_data.size;

    size_t upbytes = 0;
    while(upbytes < imgsize)
    {
        const auto upsize = imgsize - upbytes;
        const auto uprows = upbytes / rowsize;
        auto alloc = allocate(upsize);
        if(alloc.size < rowsize)
        {
            reset_allocator();
            continue;
        }
        const auto urows = (uprows % img.height);
        const auto uslices = (uprows / img.height);
        const auto alrows = (alloc.size / rowsize);
        const auto alslices = std::max((alrows / img.height), 1ull);
        assert(alslices == 1);
        const auto copy = Vks(VkBufferImageCopy2{
            .bufferOffset = alloc.offset + upbytes,
            .imageSubresource = { gfx::to_vk(img.deduce_aspect()), 0, 0, 1 },
            .imageOffset = { 0, (int32_t)(urows * block_data.y), (int32_t)(uslices * block_data.z) },
            .imageExtent = { img.width, (uint32_t)(alrows * block_data.y), (uint32_t)(alslices * block_data.z) } });
        memcpy(alloc.mem, (std::byte*)src + upbytes, alloc.size);
        get_cmd()->copy(img, buffer.get(), &copy, 1);
        upbytes += alrows * rowsize;
    }
    assert(upbytes == imgsize);
}

void StagingBuffer::barrier()
{
    get_cmd()->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW);
}

void StagingBuffer::barrier(Handle<Image> dst, ImageLayout dst_layout)
{
    assert(dst && dst_layout != ImageLayout::UNDEFINED);
    get_cmd()->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW, dst->current_layout, dst_layout);
}

void StagingBuffer::barrier(Handle<Image> dst, ImageLayout src_layout, ImageLayout dst_layout, const ImageSubRange& range)
{
    assert(dst && dst_layout != ImageLayout::UNDEFINED);
    get_cmd()->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_RW, src_layout, dst_layout, range);
}

void StagingBuffer::flush_resizes() { assert(false); }

void StagingBuffer::init(SubmitQueue* queue, const Callback<void(Handle<Buffer>)>& on_buffer_resize)
{
    auto* r = Engine::get().renderer;
    assert(r != nullptr && queue != nullptr);
    buffer = r->make_buffer(BufferDescriptor{
        "staging buffer", CAPACITY, BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT | BufferUsage::CPU_ACCESS });
    assert(buffer);
    memory = buffer->memory;
    assert(memory != nullptr);
    this->queue = queue;
    this->on_buffer_resize = on_buffer_resize;
    allocator = DoubleSidedAllocator{ memory, CAPACITY };

    int alloc_dirs[]{ 1, -1 };
    for(int i = 0; i < 2; ++i)
    {
        _dbs[i].alloc_dir = alloc_dirs[i];
        _dbs[i].cmdpool =
            queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        _dbs[i].sync = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0ull, ENG_FMT("sbufs{}", i) });
        assert(_dbs[i].cmdpool != nullptr);
        assert(_dbs[i].sync != nullptr);
        dbs[i] = &_dbs[i];
    }
    db = dbs[0];
}

Sync* StagingBuffer::flush()
{
    if(db->cmd == nullptr) { return db->sync; }
    assert(db->cmd != nullptr && db->sync != nullptr);
    // flush_resizes();
    db->cmdpool->end(db->cmd);
    queue->with_cmd_buf(db->cmd).signal_sync(db->sync, PipelineStage::TRANSFER_BIT).submit();
    db->cmd = nullptr;
    return db->sync;
}

void StagingBuffer::reset()
{
    flush();
    std::swap(dbs[0], dbs[1]);
    db = dbs[0];
    db->sync->wait_cpu(~0ull);
    db->sync->reset();
    db->cmdpool->reset();
    db->cmd = nullptr;
    allocator.reset(db->alloc_dir);
}

StagingBuffer::Allocation StagingBuffer::allocate(size_t size)
{
    const auto free_space = allocator.get_free_space();
    if(free_space < ALIGNMENT) { reset_allocator(); }
    const auto alloc_size = std::min(align_up2(size, ALIGNMENT), free_space);
    assert(alloc_size > 0 && alloc_size % ALIGNMENT == 0);
    auto* mem = allocator.alloc(alloc_size, db->alloc_dir);
    Allocation alloc{ mem, (uintptr_t)mem - (uintptr_t)memory, std::min(alloc_size, size) };
    return alloc;
}

CommandBuffer* StagingBuffer::get_cmd()
{
    if(db->cmd == nullptr) { db->cmd = db->cmdpool->begin(); }
    return db->cmd;
}

Sync* StagingBuffer::get_sync()
{
    if(syncs.empty())
    {
#ifdef ENG_SBUF_DEBUG_STATS
        ++debugstats.sync_count;
#endif
        return Engine::get().renderer->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "staging sync" });
    }
    auto* back = syncs.back();
    syncs.pop_back();
    return back;
}

void StagingBuffer::reset_allocator()
{
    flush();
    db->sync->wait_cpu(~0ull);
    allocator.reset(db->alloc_dir);
    if(allocator.get_free_space() < ALIGNMENT)
    {
        reset();                   // reset previous, as it hogs up all the memory
        std::swap(dbs[0], dbs[1]); // go back to our data storage for this frame
        db = dbs[0];
    }
    assert(allocator.get_free_space() >= ALIGNMENT);
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
