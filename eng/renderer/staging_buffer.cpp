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
    allocator = std::make_unique<LinearAllocator>(this->staging_buffer->mapped, this->staging_buffer->get_capacity());
    cmdpool = queue->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    assert(allocator && cmdpool);
}

void StagingBuffer::send_to(const Transfer& transfer)
{
    size_t uploaded_sz = 0;
    auto* r = RendererVulkan::get_instance();

    if(transfer.dst_type == ResourceType::BUFFER)
    {
        if(transfer.src_type != ResourceType::VECTOR || transfer.src_type != ResourceType::BUFFER)
        {
            assert(false);
            return;
        }

        assert(transfer.src_type != ResourceType::IMAGE);
        auto dbh = transfer.dst_buf();
        auto* db = &r->get_buffer(dbh);

        // if too small, resize
        if(db->buffer && db->get_capacity() < (transfer.dst_range.offset + transfer.dst_range.size))
        {
            auto bci = db->create_info;
            bci.size = transfer.dst_range.offset + transfer.dst_range.size;
            const auto nb = r->make_buffer(bci);
            send_to(nb, dbh, { 0, db->get_size() }, 0);
            r->destroy_buffer(dbh);
            dbh = nb;
            db = &r->get_buffer(nb);
        }
        if(!db->buffer) { db->allocate(); }

        if(transfer.src_type == ResourceType::BUFFER)
        {
            const auto cmd = cmdpool->begin();
            record_command(cmd, transfer);
            queue->with_cmd_buf(cmd).submit_wait(-1ull);
            return;
        }

        if(transfer.src_type == ResourceType::VECTOR)
        {
            while(uploaded_sz < transfer.src_range.size)
            {
                auto [pGPU, alloc_sz] = allocator->allocate_best_fit(transfer.src_range.size);
                while(!pGPU) // for future: wait for all uploads on other threads to finish so the memory frees up
                {
                    std::tie(pGPU, alloc_sz) = allocator->allocate_best_fit(transfer.src_range.size);
                }
                memcpy(pGPU, static_cast<const std::byte*>(transfer.data) + uploaded_sz, alloc_sz);
                send_to(dbh, {}, { allocator->get_alloc_offset(pGPU), alloc_sz }, transfer.dst_range.offset + uploaded_sz);
                uploaded_sz += alloc_sz;
                allocator->reset();
            }
            return;
        }
    }
}

VkImageMemoryBarrier2 StagingBuffer::generate_image_barrier(Handle<Image> image, VkImageLayout layout, bool is_final_layout)
{
    auto& r = *RendererVulkan::get_instance();
    auto& i = r.get_image(image);
    return is_final_layout
               ? Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                            .dstAccessMask = VK_ACCESS_2_NONE,
                                            .oldLayout = i.current_layout,
                                            .newLayout = layout,
                                            .image = i.image,
                                            .subresourceRange = { .aspectMask = i.deduce_aspect(),
                                                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                                                  .layerCount = VK_REMAINING_ARRAY_LAYERS } })
               : Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                            .srcAccessMask = VK_ACCESS_2_NONE,
                                            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                            .oldLayout = i.current_layout,
                                            .newLayout = layout,
                                            .image = i.image,
                                            .subresourceRange = { .aspectMask = i.deduce_aspect(),
                                                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                                                  .layerCount = VK_REMAINING_ARRAY_LAYERS } });
}

void StagingBuffer::transition_image(Handle<Image> image, VkImageLayout layout, bool is_final_layout)
{
    auto& r = *RendererVulkan::get_instance();
    auto& i = r.get_image(image);
    const auto barrier = generate_image_barrier(image, layout, is_final_layout);
    const auto dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier });
    const auto cmd = cmdpool->begin(cmds[0]);
    vkCmdPipelineBarrier2(cmd, &dep_info);
    cmdpool->end(cmd);
    queue->with_cmd_buf(cmd).submit_wait(-1ull); // wait because cmd might be pending on subsequent begins
    i.current_layout = layout;
}

void StagingBuffer::record_command(VkCommandBuffer cmd, const Transfer& transfer)
{
    if(transfer.src_type != ResourceType::STAGING)
    {
        assert(false);
        return;
    }

    auto* r = RendererVulkan::get_instance();
    auto& sb = *staging_buffer;
    assert(sb.buffer);

    if(transfer.dst_type == ResourceType::BUFFER)
    {
        const auto dbh = transfer.dst_buf();
        auto& db = r->get_buffer(dbh);
        assert(db.buffer);
        assert(sb.get_capacity() <= transfer.src_range.offset + transfer.src_range.size);
        assert(db.get_capacity() <= transfer.dst_range.offset + transfer.dst_range.size);
        VkBufferCopy copy{ .srcOffset = transfer.src_range.offset,
                           .dstOffset = transfer.dst_range.offset,
                           .size = transfer.dst_range.size };
        vkCmdCopyBuffer(cmd, sb.buffer, db.buffer, 1, &copy);
    }
}

} // namespace gfx
