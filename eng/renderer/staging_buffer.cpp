#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/utils.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <deque>

namespace gfx
{

void GPUStagingManager::init(SubmitQueue* queue, const std::function<void(Handle<Buffer>)>& on_buffer_resize)
{
    buffer = Buffer{ BufferCreateInfo{ "staging buffer", CAPACITY,
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true } };
    buffer.init();
    this->queue = queue;
    this->on_buffer_resize = on_buffer_resize;
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for(auto i = 0u; i < CMD_COUNT; ++i)
    {
        cmds[i] = { cmdpool->allocate(), RendererVulkan::get_instance()->make_sync({ SyncType::TIMELINE_SEMAPHORE, 0,
                                                                                     fmt::format("staging_sem_{}", i) }) };
    }
    begin_new_cmd_buffer();
}

void GPUStagingManager::resize(Handle<Buffer> buffer, size_t newsize)
{
    auto& old = buffer.get();
    if(newsize <= old.capacity)
    {
        old.capacity = newsize;
        old.size = newsize;
        return;
    }
    const auto info = BufferCreateInfo{ old.name, newsize, old.usage, old.mapped };
    Buffer newbuffer{ info };
    newbuffer.init();
    newbuffer.size = old.size;
    if(info.mapped) { memcpy(newbuffer.memory, old.memory, old.size); }
    else if(old.size > 0)
    {
        get_cmd()->copy(newbuffer, old, 0, { 0, old.size });
        flush()->wait_cpu(~0ull);
    }
    old.destroy();
    old = std::move(newbuffer);
    if(on_buffer_resize) { on_buffer_resize(buffer); }
}

void GPUStagingManager::copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range range)
{
    if(range.size == 0) { return; }
    if(dst_offset == STAGING_APPEND) { dst_offset = dst->size; }
    if(dst->mapped && src->mapped)
    {
        memcpy((std::byte*)dst->memory + dst_offset, (const std::byte*)src->memory + range.offset, range.size);
    }
    else { get_cmd()->copy(dst.get(), src.get(), dst_offset, range); }
    dst->size = std::max(dst->size, dst_offset + range.size);
}

void GPUStagingManager::copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, Range range)
{
    if(range.size == 0) { return; }
    if(dst_offset == STAGING_APPEND) { dst_offset = dst->size; }
    if(dst->capacity < dst_offset + range.size) { resize(dst, dst_offset + range.size); }
    if(dst->mapped) { memcpy((std::byte*)dst->memory + dst_offset, (const std::byte*)src + range.offset, range.size); }
    else
    {
        size_t uploaded = 0;
        while(uploaded < range.size)
        {
            auto [mem, size] = allocate(range.size - uploaded);
            memcpy(mem, (const std::byte*)src + range.offset + uploaded, size);
            get_cmd()->copy(dst.get(), buffer, dst_offset + uploaded, { get_offset(mem), size });
            uploaded += size;
        }
    }
    dst->size = std::max(dst->size, dst_offset + range.size);
}

void GPUStagingManager::copy(Handle<Image> dst, const void* const src, VkImageLayout final_layout)
{
    auto& img = dst.get();
    const auto total_size = img.extent.width * img.extent.height * 4;
    auto [mem, size] = allocate(total_size);
    assert(size >= total_size);
    memcpy(mem, src, total_size);
    const auto copy = Vks(VkBufferImageCopy2{
        .bufferOffset = get_offset(mem), .imageSubresource = { img.deduce_aspect(), 0, 0, 1 }, .imageExtent = img.extent });
    get_cmd()->barrier(img, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    get_cmd()->copy(img, buffer, &copy, 1);
    get_cmd()->barrier(img, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final_layout);
}

Sync* GPUStagingManager::flush()
{
    auto* sync = flush_pending();
    begin_new_cmd_buffer();
    return sync;
}

void GPUStagingManager::reset()
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

Sync* GPUStagingManager::flush_pending()
{
    if(cmdcount == 0) { return get_wrapped_cmd().sem; /*whatever...*/ }
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

void GPUStagingManager::begin_new_cmd_buffer()
{
    if(cmdcount == CMD_COUNT) { reset(); }
    auto& cmdbuf = cmds[cmdstart];
    cmdbuf.sem->wait_cpu(~0ull);
    cmdpool->begin(cmds[cmdstart].cmd);
    ++cmdcount;
}

std::pair<void*, size_t> GPUStagingManager::allocate(size_t size)
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

uint32_t GPUStagingManager::get_cmd_index() const
{
    assert(cmdcount > 0);
    return (cmdstart + cmdcount - 1) % CMD_COUNT;
}

GPUStagingManager::CmdBufWrapper& GPUStagingManager::get_wrapped_cmd()
{
    assert(cmdcount > 0);
    return cmds[get_cmd_index()];
}

CommandBuffer* GPUStagingManager::get_cmd()
{
    if(cmdcount == 0) { begin_new_cmd_buffer(); }
    return get_wrapped_cmd().cmd;
}

} // namespace gfx
