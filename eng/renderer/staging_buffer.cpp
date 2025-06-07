#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <deque>

static size_t align_up2(size_t val, size_t al) { return (val + al - 1) & ~(al - 1); }

namespace gfx
{

StagingBuffer::Batch& StagingBuffer::Batch::send(const BufferCopy& copy)
{
    bcps.push_back(copy);
    return *this;
}

StagingBuffer::Batch& StagingBuffer::Batch::send(const ImageCopy& copy)
{
    icps.push_back(copy);
    return *this;
}

void StagingBuffer::Batch::submit() { sb->submit(std::move(*this)); }

StagingBuffer::StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept
    : queue(queue), staging_buffer(&RendererVulkan::get_instance()->get_buffer(staging_buffer))
{
    if(!queue)
    {
        assert(false);
        return;
    }
    assert(this->staging_buffer && this->staging_buffer->buffer);
    allocator = std::make_unique<LinearAllocator>(this->staging_buffer->memory, this->staging_buffer->capacity);
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    assert(allocator && cmdpool);
}

void StagingBuffer::submit(Batch&& batch)
{
    if(batch.bcps.empty() && batch.icps.empty()) { return; }
    auto* r = RendererVulkan::get_instance();
    Batch b = std::move(batch); // so the client code doesn't have to sustain the lifetime of a batch.
    std::unordered_map<Handle<Buffer>, size_t> dst_szs;
    dst_szs.reserve(b.bcps.size());
    // calculated required capacities for dst buffers
    for(auto& bcp : b.bcps)
    {
        auto [it, inserted] = dst_szs.emplace(bcp.dst, 0);
        const auto& buf = r->get_buffer(bcp.dst);
        size_t prev_sz = it->second;
        if(bcp.dst_offset == STAGING_APPEND) { bcp.dst_offset = inserted ? buf.size : prev_sz; }
        it->second = std::max(prev_sz, bcp.dst_offset + bcp.src_range.size);
    }

    std::vector<std::pair<Buffer*, Buffer>> resized_bufs;
    std::vector<VkCopyBufferInfo2> resized_buf_cpis;
    std::vector<VkBufferCopy2> resized_buf_cps;
    std::vector<VkImageMemoryBarrier2> dst_layout_img_barrs;
    resized_bufs.reserve(dst_szs.size());
    resized_buf_cps.reserve(dst_szs.size());
    resized_buf_cpis.reserve(dst_szs.size());
    dst_layout_img_barrs.reserve(b.icps.size());

    // create larger buffers if their current capacity is smaller than calculated
    // optionally copy old stuff on resize if there is something to copy
    for(const auto& [bh, sz] : dst_szs)
    {
        auto& buf = r->get_buffer(bh);
        if(buf.capacity >= sz) { continue; };
        const auto& pair =
            resized_bufs.emplace_back(&buf, Buffer{ buf.dev, buf.vma, BufferCreateInfo{ buf.name, buf.usage, sz, buf.mapped } });
        if(buf.size > 0)
        {
            resized_buf_cps.push_back(Vks(VkBufferCopy2{ .srcOffset = 0, .dstOffset = 0, .size = buf.size }));
            resized_buf_cpis.push_back(Vks(VkCopyBufferInfo2{
                .srcBuffer = buf.buffer, .dstBuffer = pair.second.buffer, .regionCount = 1, .pRegions = &resized_buf_cps.back() }));
        }
    }
    // transition images to transfer dst layout
    for(const auto& icp : b.icps)
    {
        auto& i = r->get_image(icp.dst);
        dst_layout_img_barrs.push_back(Vks(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE, // stage none because at the start, global memory barrier for all memory transfers is issued
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = i.image,
            .subresourceRange = { i.deduce_aspect(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } }));
    }

    auto global_vkbarr = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                               .srcAccessMask = VK_ACCESS_2_NONE,
                                               .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                               .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT });
    bool global_barrier_issued = false;
    if(!resized_buf_cps.empty() || !dst_layout_img_barrs.empty())
    {
        VkCommandBuffer cmd = cmdpool->begin();
        VkDependencyInfo dep;
        dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &global_vkbarr });
        vkCmdPipelineBarrier2(cmd, &dep);
        for(const auto& bcp : resized_buf_cpis)
        {
            vkCmdCopyBuffer2(cmd, &bcp);
        }
        dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = (uint32_t)dst_layout_img_barrs.size(),
                                    .pImageMemoryBarriers = dst_layout_img_barrs.data() });
        vkCmdPipelineBarrier2(cmd, &dep);
        cmdpool->end(cmd);
        queue->with_cmd_buf(cmd).submit_wait(-1ull);
        global_barrier_issued = true;
    }
    if(!resized_bufs.empty())
    {
        for(auto& [buf, res_buf] : resized_bufs)
        {
            buf->deallocate();
            *buf = std::move(res_buf);
        }
    }

    const auto submit = [this](VkCommandBuffer& cmd, bool new_cmd = true) {
        cmdpool->end(cmd);
        queue->with_cmd_buf(cmd).submit_wait(-1ull);
        allocator->reset(); // needs fixing for multithreading; cannot reset while other threads are pending.
        cmdpool->reset();
        if(new_cmd) { cmd = cmdpool->begin(); }
    };

    VkCommandBuffer cmd = cmdpool->begin();
    if(!global_barrier_issued)
    {
        const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &global_vkbarr });
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    for(const auto& bcp : b.bcps)
    {
        if(bcp.src)
        {
            const auto vkcp =
                Vks(VkBufferCopy2{ .srcOffset = bcp.src_range.offset, .dstOffset = bcp.dst_offset, .size = bcp.src_range.size });
            const auto vkcpi = Vks(VkCopyBufferInfo2{ .srcBuffer = r->get_buffer(bcp.src).buffer,
                                                      .dstBuffer = r->get_buffer(bcp.dst).buffer,
                                                      .regionCount = 1,
                                                      .pRegions = &vkcp });
            vkCmdCopyBuffer2(cmd, &vkcpi);
        }
        else
        {
            if(bcp.data.empty())
            {
                ENG_WARN("Src range is empty");
                continue;
            }
            if(!bcp.data.data())
            {
                ENG_WARN_ASSERT("Src range is null");
                continue;
            }
            size_t uploaded_bytes = 0;
            while(true)
            {
                const auto dst_offset = bcp.dst_offset + uploaded_bytes;
                const auto left_sz = bcp.data.size_bytes() - uploaded_bytes;
                auto [pGPU, alloc_sz] = allocator->allocate_best_fit(left_sz);
                if(!pGPU)
                {
                    submit(cmd);
                    continue;
                }
                memcpy(pGPU, bcp.data.data() + uploaded_bytes, alloc_sz);
                const auto src_offset = allocator->get_alloc_offset(pGPU);
                const auto vkcp = Vks(VkBufferCopy2{ .srcOffset = src_offset, .dstOffset = dst_offset, .size = alloc_sz });
                const auto vkcpi = Vks(VkCopyBufferInfo2{
                    .srcBuffer = staging_buffer->buffer, .dstBuffer = r->get_buffer(bcp.dst).buffer, .regionCount = 1, .pRegions = &vkcp });
                vkCmdCopyBuffer2(cmd, &vkcpi);
                uploaded_bytes += alloc_sz;
                if(uploaded_bytes == bcp.data.size_bytes()) { break; }
            }
        }
    }

    dst_layout_img_barrs.clear();
    for(const auto& icp : b.icps)
    {
        auto [pGPU, alloc_sz] = allocator->allocate_best_fit(icp.data.size_bytes());
        if(!pGPU)
        {
            submit(cmd);
            std::tie(pGPU, alloc_sz) = allocator->allocate_best_fit(icp.data.size_bytes());
            assert(pGPU);
        }
        if(alloc_sz != icp.data.size_bytes())
        {
            ENG_WARN_ASSERT("Partial upload to images is not yet supported.");
            continue;
        }
        memcpy(pGPU, icp.data.data(), alloc_sz);
        auto& img = r->get_image(icp.dst);
        const auto vkicp = Vks(VkBufferImageCopy2{ .bufferOffset = allocator->get_alloc_offset(pGPU),
                                                   .imageSubresource = { img.deduce_aspect(), 0, 0, VK_REMAINING_ARRAY_LAYERS },
                                                   .imageExtent = img.vk_info.extent });
        const auto vkicpi = Vks(VkCopyBufferToImageInfo2{ .srcBuffer = staging_buffer->buffer,
                                                          .dstImage = img.image,
                                                          .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                          .regionCount = 1,
                                                          .pRegions = &vkicp });
        vkCmdCopyBufferToImage2(cmd, &vkicpi);
        dst_layout_img_barrs.push_back(Vks(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT, // stage none because at the start, global memory barrier for all memory transfers is issued
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = icp.final_layout,
            .image = img.image,
            .subresourceRange = { img.deduce_aspect(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } }));
        img.current_layout = icp.final_layout;
    }

    {
        const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = (uint32_t)dst_layout_img_barrs.size(),
                                               .pImageMemoryBarriers = dst_layout_img_barrs.data() });
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    {
        global_vkbarr = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                              .dstAccessMask = VK_ACCESS_2_NONE });
        const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &global_vkbarr });
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    submit(cmd);
}

} // namespace gfx
