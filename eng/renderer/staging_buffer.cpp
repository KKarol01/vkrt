#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <deque>

static size_t align_up2(size_t val, size_t al) { return (val + al - 1) & ~(al - 1); }

namespace gfx
{

StagingBuffer::StagingBuffer(SubmitQueue* queue) noexcept : queue(queue)
{
    if(!queue)
    {
        assert(false);
        return;
    }
    auto* r = RendererVulkan::get_instance();
    buffer = r->make_buffer(BufferCreateInfo{
        .name = "staging buffer",
        .size = CAPACITY,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
        .mapped = true,
    });
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    assert(buffer && cmdpool);
    data = buffer->memory;
    fence = queue->make_fence(false);
    assert(data && fence);
    begin_cmd_buffer();
}

void StagingBuffer::stage(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range src_range)
{
    dst_offset = resize_buffer(dst, dst_offset, src_range.size);
    transactions.push_back(Transaction{ *dst, *src, dst_offset, src_range, true, true });
    dst->size = std::max(dst->size, dst_offset + src_range.size);
}

void StagingBuffer::stage(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size)
{
    const auto num_splits = (src_size + CAPACITY - 1) / CAPACITY;
    dst_offset = resize_buffer(dst, dst_offset, src_size);
    for(auto i = 0ull; i < num_splits; ++i)
    {
        const auto size = std::min(src_size - CAPACITY * i, CAPACITY);
        auto [pGPU, pOffset] = allocate(size);
        memcpy(pGPU, src, size);
        transactions.push_back(Transaction{ *dst, *buffer, dst_offset + i * CAPACITY, { pOffset, src_size }, true, true, pGPU });
    }
    get_buffer(dst).size = std::max(get_buffer(dst).size, dst_offset + src_size);
}

void StagingBuffer::stage(Handle<Buffer> dst, std::span<const std::byte> src, size_t dst_offset)
{
    stage(dst, src.data(), dst_offset, src.size_bytes());
}

void StagingBuffer::stage(Handle<Image> dst, std::span<const std::byte> src, VkImageLayout final_layout)
{
    auto [pGPU, pOffset] = allocate(src.size_bytes());
    memcpy(pGPU, src.data(), src.size_bytes());
    transactions.push_back(Transaction{ *dst, *buffer, 0ull, { pOffset, src.size_bytes() }, false, true, pGPU, final_layout });
}

void StagingBuffer::flush()
{
    if(transactions.empty()) { return; }

    record_replacement_buffers();

    std::vector<VkImageMemoryBarrier2> img_barriers;
    std::vector<VkImageLayout> img_final_layouts;
    img_barriers.reserve(transactions.size());
    img_final_layouts.reserve(transactions.size());
    for(const auto& t : transactions)
    {
        if(!t.dst_is_buffer)
        {
            img_barriers.push_back(create_layout_transition(t.dst_image().get(), VK_IMAGE_LAYOUT_UNDEFINED,
                                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_NONE, VK_ACCESS_NONE,
                                                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT));
            img_final_layouts.push_back(t.final_layout);
            t.dst_image()->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
    }
    record_mem_barrier(img_barriers);

    for(const auto& t : transactions)
    {
        if(t.dst_is_buffer)
        {
            if(t.src_is_buffer)
            {
                record_copy(get_buffer(t.dst_buffer()), get_buffer(t.src_buffer()), t.dst_offset, t.src_range);
            }
            else { ENG_ERROR("Unsupported source type."); }
        }
        else /*dst is image*/
        {
            if(t.alloc) { record_copy(t.dst_image().get(), t.src_range.offset); }
            else { ENG_ERROR("Unsupported source type"); }
            auto h = t.dst_image()->current_layout = t.final_layout;
        }
    }

    for(auto i = 0ull; i < img_barriers.size(); ++i)
    {
        auto& b = img_barriers.at(i);
        auto l = img_final_layouts.at(i);
        b.srcStageMask = b.dstStageMask;
        b.srcAccessMask = b.dstAccessMask;
        b.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; // can this be stage none if last barrier is global?
        b.dstAccessMask = VK_ACCESS_NONE;
        b.oldLayout = b.newLayout;
        b.newLayout = l;
    }
    record_mem_barrier(img_barriers);
    record_mem_barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    cmdpool->end(cmd);
    queue->with_cmd_buf(cmd).submit_wait(~0ull);
    for(auto& [h, nb] : replacement_buffers)
    {
        if(!h) { continue; }
        std::swap(h.get(), nb);
        RendererVulkan::get_instance()->update_resource(h);
        nb.destroy();
    }
    head = 0;
    cmdpool->reset();
    transactions.clear();
    replacement_buffers.clear();
    begin_cmd_buffer();
}

void* StagingBuffer::try_allocate(size_t size)
{
    const auto aligned_size = align(size);
    if(CAPACITY < aligned_size)
    {
        ENG_ERROR("Cannot allocate more than the buffer");
        return nullptr;
    }
    if(get_free_space() < aligned_size) { return nullptr; }
    auto* ptr = head_to_ptr();
    head += aligned_size;
    return ptr;
}

std::pair<void*, size_t> StagingBuffer::allocate(size_t size)
{
    auto* ptr = try_allocate(size);
    if(!ptr)
    {
        flush();
        ptr = try_allocate(size);
    }
    if(!ptr) { ENG_ERROR("Allocation failed"); }
    return { ptr, calc_alloc_head(ptr) };
}

size_t StagingBuffer::resize_buffer(Handle<Buffer> hbuf, size_t dst_offset, size_t src_size)
{
    Buffer& buf = get_buffer(hbuf);
    if(dst_offset == STAGING_APPEND) { dst_offset = buf.size; }
    const auto capacity = dst_offset + src_size;
    if(buf.capacity < capacity)
    {
        if(auto it = std::find_if(replacement_buffers.begin(), replacement_buffers.end(),
                                  [hbuf](const auto& pair) { return pair.first == hbuf; });
           it != replacement_buffers.end())
        {
            it->second.capacity = capacity;
        }
        else
        {
            Buffer b{ BufferCreateInfo{ buf.name, capacity, buf.usage, buf.mapped } };
            replacement_buffers.emplace_back(hbuf, b);
        }
    }
    return dst_offset;
}

void StagingBuffer::begin_cmd_buffer()
{
    cmd = cmdpool->begin();
    // wait for all the previous accesses
    record_mem_barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}

void StagingBuffer::record_mem_barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                       VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
    const auto bar = Vks(VkMemoryBarrier2{
        .srcStageMask = src_stage, .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access });
    const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void StagingBuffer::record_mem_barrier(std::span<const VkImageMemoryBarrier2> barriers)
{
    const auto dep =
        Vks(VkDependencyInfo{ .imageMemoryBarrierCount = (uint32_t)barriers.size(), .pImageMemoryBarriers = barriers.data() });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void StagingBuffer::record_replacement_buffers()
{
    bool issue_barrier = false;
    std::vector<VkBufferCopy2> buf_copies;
    buf_copies.reserve(replacement_buffers.size());
    for(auto& [h, nb] : replacement_buffers)
    {
        nb.init();
        if(h->size != 0)
        {
            record_copy(nb, h.get(), 0ull, { 0ull, h->size });
            issue_barrier = true;
        }
    }
    if(issue_barrier)
    {
        // src stage none because of global barrier issued at the begin of the cmd buffer
        record_mem_barrier(VK_PIPELINE_STAGE_NONE, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    }
}

void StagingBuffer::record_copy(Buffer& dst, Buffer& src, size_t dst_offset, Range src_range)
{
    const auto copy = create_copy(dst_offset, src_range);
    const auto info =
        Vks(VkCopyBufferInfo2{ .srcBuffer = src.buffer, .dstBuffer = dst.buffer, .regionCount = 1, .pRegions = &copy });
    vkCmdCopyBuffer2(cmd, &info);
}

void StagingBuffer::record_copy(Image& dst, size_t src_offset)
{
    const auto copy = Vks(VkBufferImageCopy2{
        .bufferOffset = src_offset, .imageSubresource = { dst.deduce_aspect(), 0, 0, 1 }, .imageExtent = dst.extent });
    const auto info = Vks(VkCopyBufferToImageInfo2{ .srcBuffer = buffer->buffer,
                                                    .dstImage = dst.image,
                                                    .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                    .regionCount = 1,
                                                    .pRegions = &copy });
    vkCmdCopyBufferToImage2(cmd, &info);
}

VkBufferCopy2 StagingBuffer::create_copy(size_t dst_offset, Range src_range) const
{
    return Vks(VkBufferCopy2{ .srcOffset = src_range.offset, .dstOffset = dst_offset, .size = src_range.size });
}

VkImageMemoryBarrier2 StagingBuffer::create_layout_transition(const Image& img, VkImageLayout src_layout, VkImageLayout dst_layout,
                                                              VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                                              VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) const
{
    return Vks(VkImageMemoryBarrier2{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = src_layout,
        .newLayout = dst_layout,
        .image = img.image,
        .subresourceRange = { img.deduce_aspect(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } });
}

Buffer& StagingBuffer::get_buffer(Handle<Buffer> buffer)
{
    if(auto it = std::find_if(replacement_buffers.begin(), replacement_buffers.end(),
                              [buffer](const auto& e) { return e.first == buffer; });
       it != replacement_buffers.end())
    {
        return it->second;
    }
    return buffer.get();
}

Handle<Buffer> StagingBuffer::Transaction::dst_buffer() const { return Handle<Buffer>{ dst_resource }; }

Handle<Image> StagingBuffer::Transaction::dst_image() const { return Handle<Image>{ dst_resource }; }

Handle<Buffer> StagingBuffer::Transaction::src_buffer() const { return Handle<Buffer>{ src_resource }; }

Handle<Image> StagingBuffer::Transaction::src_image() const { return Handle<Image>{ src_resource }; }

} // namespace gfx
