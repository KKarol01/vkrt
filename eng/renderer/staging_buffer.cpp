#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/utils.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/to_vk.hpp>

namespace eng
{
namespace gfx
{

void StagingBuffer::init(SubmitQueue* queue, const Callback<void(Handle<Buffer>)>& on_buffer_resize)
{
    auto* r = Engine::get().renderer;
    buffer = Buffer{ BufferDescriptor{ "staging buffer", CAPACITY,
                                       BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT | BufferUsage::CPU_ACCESS } };
    VkBufferMetadata::init(buffer);
    this->queue = queue;
    this->on_buffer_resize = on_buffer_resize;
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for(auto i = 0u; i < CMD_COUNT; ++i)
    {
        cmds[i] = { CmdBufWrapper::INITIAL, cmdpool->allocate(),
                    r->make_sync({ SyncType::TIMELINE_SEMAPHORE, 0, ENG_FMT("staging_sem_{}", i) }) };
    }
    pending.reserve(cmds.size());
    reset();
}

void StagingBuffer::resize(Handle<Buffer> buffer, size_t newsize)
{
    flush()->wait_cpu(~0ull);
    auto& old = buffer.get();
    if(newsize <= old.capacity)
    {
        old.size = std::min(old.size, newsize);
        return;
    }
    const auto info = BufferDescriptor{ old.name, newsize, old.usage };
    Buffer newbuffer{ info };
    VkBufferMetadata::init(newbuffer);
    newbuffer.size = old.size;
    if(newbuffer.memory) { memcpy(newbuffer.memory, old.memory, old.size); }
    else if(old.size > 0)
    {
        get_cmd()->copy(newbuffer, old, 0, { 0, old.size });
        flush()->wait_cpu(~0ull);
    }
    VkBufferMetadata::destroy(old);
    old = std::move(newbuffer);
    if(on_buffer_resize) { on_buffer_resize(buffer); }
}

void StagingBuffer::copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range range)
{
    if(range.size == 0) { return; }
    if(dst_offset == STAGING_APPEND) { dst_offset = dst->size; }
    if(dst->capacity < dst_offset + range.size) { resize(dst, dst_offset + range.size); }
    if(dst->memory && src->memory)
    {
        memcpy((std::byte*)dst->memory + dst_offset, (const std::byte*)src->memory + range.offset, range.size);
    }
    else
    {
        get_cmd()->barrier(PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW);
        get_cmd()->copy(dst.get(), src.get(), dst_offset, range);
        get_cmd()->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_RW, PipelineStage::ALL, PipelineAccess::NONE);
    }
    dst->size = std::max(dst->size, dst_offset + range.size);
}

void StagingBuffer::copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size)
{
    if(src_size == 0) { return; }
    if(dst_offset == STAGING_APPEND) { dst_offset = dst->size; }
    if(dst->capacity < dst_offset + src_size) { resize(dst, dst_offset + src_size); }
    if(dst->memory) { memcpy((std::byte*)dst->memory + dst_offset, src, src_size); }
    else
    {
        size_t uploaded = 0;
        while(uploaded < src_size)
        {
            auto alloc = allocate(src_size - uploaded);
            memcpy(alloc.mem, (const std::byte*)src + uploaded, alloc.size);
            get_cmd()->copy(dst.get(), buffer, dst_offset + uploaded, { alloc.offset, alloc.size });
            uploaded += alloc.size;
        }
    }
    dst->size = std::max(dst->size, dst_offset + src_size);
}

void StagingBuffer::copy(Handle<Image> dst, const void* const src, ImageLayout final_layout)
{
    auto& img = dst.get();
    const auto block_data = GetBlockData(img.format);
    const auto bx = img.width / block_data.x;
    const auto by = img.height / block_data.y;
    const auto bz = img.depth / block_data.z;
    const auto bcount = bx * by * bz;
    const auto rowsize = bx * block_data.size;
    const auto imgsize = bcount * block_data.size;

    get_cmd()->barrier(img, PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::UNDEFINED, ImageLayout::TRANSFER_DST);
    size_t upbytes = 0;
    while(upbytes < imgsize)
    {
        const auto upsize = imgsize - upbytes;
        const auto uprows = upbytes / rowsize;
        auto alloc = allocate(upsize);
        if(alloc.size < rowsize)
        {
            reset();
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
        get_cmd()->copy(img, buffer, &copy, 1);
        upbytes += alrows * rowsize;
    }
    get_cmd()->barrier(img, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::ALL,
                       PipelineAccess::NONE, ImageLayout::TRANSFER_DST, final_layout);

    assert(upbytes == imgsize);
}

void StagingBuffer::copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy, ImageLayout dst_final_layout)
{
    // dst and src may be the same. if that's the case, need to make sure to check if ranges overlap.
    const auto oldsrcl = src->current_layout;
    assert(oldsrcl != ImageLayout::UNDEFINED);
    const auto dstrange = ImageSubRange{ .mips = { copy.dstlayers.mip, 1 }, .layers = copy.dstlayers.layers };
    const auto srcrange = ImageSubRange{ .mips = { copy.srclayers.mip, 1 }, .layers = copy.srclayers.layers };
    get_cmd()->barrier(dst.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_WRITE_BIT, dst->current_layout, ImageLayout::TRANSFER_DST, dstrange);
    get_cmd()->barrier(src.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_READ_BIT, src->current_layout, ImageLayout::TRANSFER_SRC, srcrange);
    get_cmd()->copy(dst.get(), src.get(), copy, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC);
    get_cmd()->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::ALL,
                       PipelineAccess::NONE, ImageLayout::TRANSFER_DST, dst_final_layout, dstrange);
    get_cmd()->barrier(src.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT, PipelineStage::ALL,
                       PipelineAccess::NONE, ImageLayout::TRANSFER_SRC, oldsrcl, srcrange);
}

void StagingBuffer::blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit, ImageLayout dst_final_layout)
{
    const auto oldsrcl = src->current_layout;
    assert(oldsrcl != ImageLayout::UNDEFINED);
    const auto dstrange = ImageSubRange{ .mips = { blit.dstlayers.mip, 1 }, .layers = blit.dstlayers.layers };
    const auto srcrange = ImageSubRange{ .mips = { blit.srclayers.mip, 1 }, .layers = blit.srclayers.layers };
    get_cmd()->barrier(dst.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_WRITE_BIT, dst->current_layout, ImageLayout::TRANSFER_DST, dstrange);
    get_cmd()->barrier(src.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_READ_BIT, src->current_layout, ImageLayout::TRANSFER_SRC, srcrange);

    get_cmd()->blit(dst.get(), src.get(), blit, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC, ImageFilter::LINEAR);
    get_cmd()->barrier(dst.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::ALL,
                       PipelineAccess::NONE, ImageLayout::TRANSFER_DST, dst_final_layout, dstrange);
    get_cmd()->barrier(src.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT, PipelineStage::ALL,
                       PipelineAccess::NONE, ImageLayout::TRANSFER_SRC, oldsrcl, srcrange);
}

Sync* StagingBuffer::flush()
{
    if(pending.empty()) { return get_wrapper().sem; }
    auto* sem = pending.front()->sem;
    for(auto& e : pending)
    {
        if(e->state == CmdBufWrapper::RECORDING) { cmdpool->end(e->cmd); }
        e->state = CmdBufWrapper::PENDING;
        queue->with_cmd_buf(e->cmd);
        queue->signal_sync(e->sem, PipelineStage::TRANSFER_BIT);
    }
    queue->submit();
    pending.clear();
    begin_new_cmd_buffer();
    return sem;
}

void StagingBuffer::reset()
{
    flush();
    for(auto i = 0u; i < cmds.size(); ++i)
    {
        if(cmds.at(i).state == CmdBufWrapper::PENDING)
        {
            cmds.at(i).sem->wait_cpu(~0ull);
            cmds.at(i).sem->reset();
        }
        cmds.at(i).state = CmdBufWrapper::INITIAL;
    }
    head = 0;
    cmdhead = 0;
    begin_new_cmd_buffer();
    return;
}

void StagingBuffer::begin_new_cmd_buffer()
{
    for(auto i = 0u; i < cmds.size(); ++i)
    {
        const auto cmdi = (cmdhead + i) & (cmds.size() - 1);
        auto& cmd = cmds.at(cmdi);
        if(cmd.state == CmdBufWrapper::INITIAL || cmd.state == CmdBufWrapper::RECORDING)
        {
            if(cmd.state == CmdBufWrapper::INITIAL)
            {
                cmd.state = CmdBufWrapper::RECORDING;
                cmdpool->begin(cmd.cmd);
                pending.push_back(&cmd);
            }
            cmdhead = cmdi;
            return;
        }
    }
    reset();
    begin_new_cmd_buffer();
}

StagingBuffer::Allocation StagingBuffer::allocate(size_t size)
{
    auto free_space = CAPACITY - head;
    if(free_space == 0)
    {
        reset();
        free_space = CAPACITY;
    }
    assert(free_space >= ALIGNMENT && free_space % ALIGNMENT == 0);
    const auto aligned_size = std::min(align_up2(size, ALIGNMENT), free_space);
    assert(aligned_size % ALIGNMENT == 0 && aligned_size <= free_space);
    void* mem = (std::byte*)buffer.memory + head;
    head += aligned_size;
    return { &buffer, mem, (uintptr_t)mem - (uintptr_t)buffer.memory, std::min(size, aligned_size) };
}

} // namespace gfx
} // namespace eng
