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

    const auto calculate_required_dst_buf_sizes = [&b, r] {
        std::unordered_map<Handle<Buffer>, size_t> dst_szs;
        dst_szs.reserve(b.bcps.size());
        for(auto& bcp : b.bcps)
        {
            auto [it, inserted] = dst_szs.emplace(bcp.dst, 0);
            const auto& buf = r->get_buffer(bcp.dst);
            size_t prev_sz = it->second;
            if(bcp.dst_offset == STAGING_APPEND) { bcp.dst_offset = inserted ? buf.size : prev_sz; }
            it->second = std::max(prev_sz, bcp.dst_offset + bcp.src_range.size);
        }
        return dst_szs;
    };

    const auto create_resized_buffers = [r](const auto& dst_szs) {
        std::vector<std::pair<Buffer*, Buffer>> resized_bufs;
        resized_bufs.reserve(dst_szs.size());
        for(const auto& [bh, req_sz] : dst_szs)
        {
            auto& buf = r->get_buffer(bh);
            if(buf.capacity < req_sz)
            {
                resized_bufs.emplace_back(&buf, Buffer{ buf.dev, buf.vma,
                                                        BufferCreateInfo{ buf.name, buf.usage, req_sz, buf.mapped } });
            }
        }
        return resized_bufs;
    };

    const auto record_buffer_resize_copies = [](auto cmd, const auto& res_bufs) -> bool {
        bool recorded = false;
        for(const auto& [ob, nb] : res_bufs)
        {
            if(ob->size > 0)
            {
                const auto vkcp = Vks(VkBufferCopy2{ .srcOffset = 0, .dstOffset = 0, .size = ob->size });
                const auto vkcpi =
                    Vks(VkCopyBufferInfo2{ .srcBuffer = ob->buffer, .dstBuffer = nb.buffer, .regionCount = 1, .pRegions = &vkcp });
                vkCmdCopyBuffer2(cmd, &vkcpi);
                recorded = true;
            }
        }
        return recorded;
    };

    const auto submit = [this](VkCommandBuffer& cmd, bool new_cmd = true) {
        cmdpool->end(cmd);
        queue->with_cmd_buf(cmd).submit_wait(-1ull);
        allocator->reset(); // needs fixing for multithreading; cannot reset while other threads are pending.
        cmdpool->reset();
        if(new_cmd) { cmd = cmdpool->begin(); }
    };

    const auto resize_buffers = [&calculate_required_dst_buf_sizes, &create_resized_buffers,
                                 &record_buffer_resize_copies, &submit](auto& cmd) -> bool {
        const auto dst_szs = calculate_required_dst_buf_sizes();
        auto res_bufs = create_resized_buffers(dst_szs);
        const auto recorded_copies = record_buffer_resize_copies(cmd, res_bufs);
        if(recorded_copies) { submit(cmd); }
        for(auto& [buf, res_buf] : res_bufs)
        {
            *buf = std::move(res_buf);
        }
        return recorded_copies;
    };

    const auto record_img_bar = [](auto cmd, auto srcstage, auto srcaccess, auto dststage, auto dstaccess,
                                   auto oldlayout, auto newlayout, auto& image) {
        const auto ibar = Vks(VkImageMemoryBarrier2{
            .srcStageMask = srcstage,
            .srcAccessMask = srcaccess,
            .dstStageMask = dststage,
            .dstAccessMask = dstaccess,
            .oldLayout = oldlayout,
            .newLayout = newlayout,
            .image = image.image,
            .subresourceRange = { image.deduce_aspect(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } });
        const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &ibar });
        vkCmdPipelineBarrier2(cmd, &dep);
        image.current_layout = newlayout;
    };

    const auto record_global_barrier = [](auto cmd, auto srcstage, auto srcaccess, auto dststage, auto dstaccess) {
        const auto bar = Vks(VkMemoryBarrier2{
            .srcStageMask = srcstage, .srcAccessMask = srcaccess, .dstStageMask = dststage, .dstAccessMask = dstaccess });
        const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
        vkCmdPipelineBarrier2(cmd, &dep);
    };

    auto cmd = cmdpool->begin();
    record_global_barrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    resize_buffers(cmd);
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
        record_img_bar(cmd, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, img);
        vkCmdCopyBufferToImage2(cmd, &vkicpi);
        record_img_bar(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, icp.final_layout, img);
    }

    record_global_barrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    submit(cmd);
}

} // namespace gfx
