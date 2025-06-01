#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <deque>

static size_t align_up2(size_t val, size_t al) { return (val + al - 1) & ~(al - 1); }

namespace gfx
{

StagingBuffer::StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept
    : queue(queue), staging_buffer(&RendererVulkan::get_instance()->get_buffer(staging_buffer))
{
    if(!queue)
    {
        assert(false);
        return;
    }
    assert(this->staging_buffer && this->staging_buffer->buffer);
    allocator = std::make_unique<LinearAllocator>(this->staging_buffer->mapped, this->staging_buffer->get_capacity());
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    assert(allocator && cmdpool);
}

void StagingBuffer::send_to(Handle<Buffer> dst, Handle<Buffer> src, Range src_range, size_t dst_offset)
{
    send_to(Transfer{ .src_res = *src,
                      .src_type = ResourceType::BUFFER,
                      .dst_res = *dst,
                      .dst_type = ResourceType::BUFFER,
                      .src_range = src_range,
                      .dst_range = { dst_offset, src_range.size } });
}

void StagingBuffer::submit()
{
    if(pending.empty()) { return; }

    auto* r = RendererVulkan::get_instance();
    VkCommandBuffer cmd = cmdpool->begin();

    const auto submit = [this, &cmd](bool make_new = true) {
        cmdpool->end(cmd);
        queue->with_cmd_buf(cmd).submit_wait(-1ull);
        if(make_new) { cmd = cmdpool->begin(); }
    };

    // initial barrier
    {
        const auto vkmembar =
            Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                  .srcAccessMask = VK_ACCESS_2_NONE,
                                  .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT });
        const auto vkdep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &vkmembar });
        vkCmdPipelineBarrier2(cmd, &vkdep);
    }

    {
        std::unordered_map<Handle<Buffer>, size_t> dst_buf_sizes;
        std::unordered_map<Handle<Buffer>, Buffer> dst_buf_replacements;
        for(auto& p : pending)
        {
            if(p.dst_type != ResourceType::BUFFER) { continue; }
            const auto bh = p.dst_buf();
            auto& b = r->get_buffer(bh);
            if(!dst_buf_sizes.contains(bh)) { dst_buf_sizes[bh] = b.get_size(); }
            if(p.dst_range.offset == ~0ull) { p.dst_range.offset = dst_buf_sizes.at(bh); }
            const auto req_sz = p.dst_range.offset + p.dst_range.size;
            dst_buf_sizes.at(bh) = std::max(dst_buf_sizes.at(bh), req_sz);
        }
        for(auto [h, sz] : dst_buf_sizes)
        {
            auto& b = r->get_buffer(h);
            if(!b.buffer)
            {
                b.create_info.size = sz;
                b.allocate();
            }
            else if(b.get_capacity() < sz)
            {
                Buffer nb{ b.dev, b.vma, b.create_info };
                nb.create_info.size = sz;
                nb.allocate();
                if(b.get_size() > 0)
                {
                    const auto vkbufcopy = Vks(VkBufferCopy{ .srcOffset = 0, .dstOffset = 0, .size = b.get_size() });
                    vkCmdCopyBuffer(cmd, b.buffer, nb.buffer, 1, &vkbufcopy);
                }
                dst_buf_replacements[h] = std::move(nb);
            }
        }
        if(!dst_buf_replacements.empty())
        {
            const auto vkmembar =
                Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                      .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                      .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT });
            const auto vkdep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &vkmembar });
            vkCmdPipelineBarrier2(cmd, &vkdep);
            submit();
        }
        for(auto& [h, nb] : dst_buf_replacements)
        {
            auto& b = r->get_buffer(h);
            b.deallocate();
            b = std::move(nb);
            r->update_buffer(h);
        }
    }

    for(auto i = 0u; i < pending.size(); ++i)
    {
        Transfer& t = pending.at(i);

        if(t.src_type == ResourceType::VECTOR)
        {
            size_t uploaded = 0;
            while(true)
            {
                auto [pGPU, szGPU] = allocator->allocate_best_fit(t.src_range.size);
                if(pGPU) { memcpy(pGPU, static_cast<const std::byte*>(t.data) + t.src_range.offset + uploaded, szGPU); }
                else
                {
                    submit();
                    allocator->reset();
                    continue;
                }
                if(t.dst_type == ResourceType::BUFFER)
                {
                    auto& buff = r->get_buffer(t.dst_buf());
                    const auto vkbufcopy = Vks(VkBufferCopy{ .srcOffset = allocator->get_alloc_offset(pGPU),
                                                             .dstOffset = t.dst_range.offset + uploaded,
                                                             .size = szGPU });
                    vkCmdCopyBuffer(cmd, staging_buffer->buffer, buff.buffer, 1, &vkbufcopy);
                }
                else if(t.dst_type == ResourceType::IMAGE)
                {
                    if(t.src_range.size != szGPU)
                    {
                        ENG_TODO("Implement image upload in segments, not in full, all at once.");
                        assert(false);
                        break;
                    }
                    auto& img = r->get_image(t.dst_img());
                    const auto vkimgreg = Vks(VkBufferImageCopy2{ .bufferOffset = allocator->get_alloc_offset(pGPU),
                                                                  .imageSubresource = { img.deduce_aspect(), 0, 0, 1 },
                                                                  .imageExtent = img.vk_info.extent });

                    const auto vkimgcpi = Vks(VkCopyBufferToImageInfo2{ .srcBuffer = staging_buffer->buffer,
                                                                        .dstImage = img.image,
                                                                        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                        .regionCount = 1,
                                                                        .pRegions = &vkimgreg });
                    record_image_transition(cmd, t, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    vkCmdCopyBufferToImage2(cmd, &vkimgcpi);
                }
                else
                {
                    ENG_TODO();
                    assert(false);
                }

                uploaded += szGPU;
                if(uploaded == t.src_range.size)
                {
                    if(t.dst_type == ResourceType::IMAGE) { record_image_transition(cmd, t, true, t.dst_final_layout); }
                    break;
                }
            }
        }
        else if(t.src_type == ResourceType::BUFFER)
        {
            if(t.dst_type == ResourceType::BUFFER)
            {
                record_buffer_copy(cmd, r->get_buffer(t.dst_buf()).buffer, r->get_buffer(t.src_buf()).buffer,
                                   t.src_range, t.dst_range.offset);
            }
            else { ENG_WARN_ASSERT("Unsupported dst type ({}) from src BUFFER", std::to_underlying(t.dst_type)); }
        }
        else
        {
            ENG_WARN_ASSERT("Unsupported source resource type ({})", std::to_underlying(t.src_type));
            continue;
        }
    }

    const auto vkbufbar = Vks(VkMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                .dstAccessMask = VK_ACCESS_2_NONE });
    const auto vkdep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &vkbufbar });
    vkCmdPipelineBarrier2(cmd, &vkdep);
    submit(false);
    cmdpool->reset();
    pending.clear();
}

void StagingBuffer::send_to(const Transfer& transfer)
{
    if(transfer.src_type == ResourceType::VECTOR)
    {
        if(!transfer.data || transfer.src_range.size == 0)
        {
            ENG_WARN_ASSERT("Invalid vector or size is 0");
            return;
        }
    }
    else if(transfer.src_type == ResourceType::BUFFER)
    {
        if(!transfer.src_buf())
        {
            ENG_WARN_ASSERT("Invalid src buffer handle.");
            return;
        }
        const auto& b = RendererVulkan::get_instance()->get_buffer(transfer.src_buf());
        if(!b.buffer || b.get_size() < transfer.src_range.offset + transfer.src_range.size)
        {
            ENG_WARN_ASSERT("Source buffer is not allocated or source range exceeds it's boundaries");
            return;
        }
    }
    if(transfer.dst_type == ResourceType::BUFFER)
    {
        if(!transfer.dst_buf())
        {
            ENG_WARN_ASSERT("Invalid dst buffer handle.");
            return;
        }
    }

    pending.push_back(transfer);
}

void StagingBuffer::record_image_transition(VkCommandBuffer cmd, const Transfer& transfer, bool is_final_layout, VkImageLayout layout)
{
    auto& r = *RendererVulkan::get_instance();
    auto& i = r.get_image(transfer.dst_img());
    const auto vkimgbar = [&transfer, &i, layout, is_final_layout] {
        auto bar = Vks(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = i.current_layout,
            .newLayout = layout,
            .image = i.image,
            .subresourceRange = { i.deduce_aspect(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } });
        if(is_final_layout)
        {
            bar.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            bar.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
            bar.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            bar.dstAccessMask = VK_ACCESS_2_NONE;
        }
        return bar;
    }();
    i.current_layout = layout;
    const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &vkimgbar });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void StagingBuffer::record_buffer_copy(VkCommandBuffer cmd, VkBuffer dst, VkBuffer src, Range src_range, size_t dst_offset)
{
    const auto vkbufcopy = Vks(VkBufferCopy{ .srcOffset = src_range.offset, .dstOffset = dst_offset, .size = src_range.size });
    vkCmdCopyBuffer(cmd, src, dst, 1, &vkbufcopy);
}

} // namespace gfx
