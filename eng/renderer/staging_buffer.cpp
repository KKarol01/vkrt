#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/utils.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/to_vk.hpp>
#include <deque>

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
        cmds[i] = { cmdpool->allocate(), r->make_sync({ SyncType::TIMELINE_SEMAPHORE, 0, ENG_FMT("staging_sem_{}", i) }) };
    }
    dummy_sem = r->make_sync({ SyncType::TIMELINE_SEMAPHORE, 1, "staging_dummy_sem" });
    begin_new_cmd_buffer();
}

void StagingBuffer::resize(Handle<Buffer> buffer, size_t newsize)
{
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
    else { get_cmd()->copy(dst.get(), src.get(), dst_offset, range); }
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
            auto [mem, size] = allocate(src_size - uploaded);
            memcpy(mem, (const std::byte*)src + uploaded, size);
            get_cmd()->copy(dst.get(), buffer, dst_offset + uploaded, { get_offset(mem), size });
            uploaded += size;
        }
    }
    dst->size = std::max(dst->size, dst_offset + src_size);
}

void StagingBuffer::copy(Handle<Image> dst, const void* const src, ImageLayout final_layout)
{
    auto& img = dst.get();
    const auto total_size = img.width * img.height * 4;
    auto [mem, size] = allocate(total_size);
    assert(size >= total_size);
    memcpy(mem, src, total_size);
    const auto copy = Vks(VkBufferImageCopy2{ .bufferOffset = get_offset(mem),
                                              .imageSubresource = { gfx::to_vk(img.deduce_aspect()), 0, 0, 1 },
                                              .imageExtent = { img.width, img.height, img.depth } });
    get_cmd()->barrier(img, PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::UNDEFINED, ImageLayout::TRANSFER_DST);
    get_cmd()->copy(img, buffer, &copy, 1);
    get_cmd()->barrier(img, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::ALL,
                       PipelineAccess::NONE, ImageLayout::TRANSFER_DST, final_layout);
}

void StagingBuffer::insert_barrier()
{
    get_cmd()->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::TRANSFER_BIT,
                       PipelineAccess::TRANSFER_WRITE_BIT);
}

Sync* StagingBuffer::flush()
{
    auto* sync = flush_pending();
    begin_new_cmd_buffer();
    return sync;
}

void StagingBuffer::reset()
{
    flush_pending();
    for(auto i = 0u; i < CMD_COUNT; ++i)
    {
        cmds[i].sem->wait_cpu(~0ull);
        cmds[i].sem->reset();
    }
    head = 0;
    cmdstart = 0;
    cmdcount = 0;
}

Sync* StagingBuffer::flush_pending()
{
    if(cmdcount == 0) { return dummy_sem; }
    for(auto i = 0u; i < cmdcount; ++i)
    {
        const auto idx = (cmdstart + i) % CMD_COUNT;
        const auto& cmd = cmds[idx];
        cmdpool->end(cmd.cmd);
        queue->with_cmd_buf(cmd.cmd);
        queue->signal_sync(cmd.sem);
    }
    queue->submit();
    auto* sync = cmds[cmdstart].sem;
    cmdstart = (cmdstart + cmdcount) % CMD_COUNT;
    cmdcount = 0;
    return sync;
}

void StagingBuffer::begin_new_cmd_buffer()
{
    if(cmdcount == CMD_COUNT) { reset(); }
    auto& cmdbuf = cmds[cmdstart];
    cmdbuf.sem->wait_cpu(~0ull);
    cmdpool->begin(cmds[cmdstart].cmd);
    ++cmdcount;
}

std::pair<void*, size_t> StagingBuffer::allocate(size_t size)
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
    void* const mem = (std::byte*)buffer.memory + head;
    head += aligned_size;
    return { mem, std::min(size, aligned_size) };
}

uint32_t StagingBuffer::get_cmd_index() const
{
    assert(cmdcount > 0);
    return (cmdstart + cmdcount - 1) % CMD_COUNT;
}

StagingBuffer::CmdBufWrapper& StagingBuffer::get_wrapped_cmd()
{
    assert(cmdcount > 0);
    return cmds[get_cmd_index()];
}

CommandBuffer* StagingBuffer::get_cmd()
{
    if(cmdcount == 0) { begin_new_cmd_buffer(); }
    return get_wrapped_cmd().cmd;
}

} // namespace gfx
} // namespace eng
