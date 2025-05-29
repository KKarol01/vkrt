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

void StagingBuffer::upload()
{
    auto* r = RendererVulkan::get_instance();

    if(transfer.dst_type == ResourceType::BUFFER)
    {
        if(transfer.src_type != ResourceType::VECTOR && transfer.src_type != ResourceType::BUFFER)
        {
            assert(false);
            return;
        }

        if(transfer.src_range.size == 0) { return; } // don't upload empty

        auto dbh = transfer.dst_buf();
        auto* db = &r->get_buffer(dbh);
        const auto req_size = transfer.dst_range.offset + transfer.dst_range.size;

        // if too small, resize
        if(db->buffer && db->get_capacity() < req_size)
        {
            auto ndbci = db->create_info;
            ndbci.size = req_size;
            const auto ndbh = r->make_buffer(ndbci);      // this will allocate, since size is known.
            send_to(ndbh, dbh, { 0, db->get_size() }, 0); // copy old data
            r->destroy_buffer(dbh);
            dbh = ndbh;
            db = &r->get_buffer(ndbh);
        }
        // buffer is null on buffers created with size 0.
        else if(!db->buffer)
        {
            db->create_info.size = std::max(db->create_info.size, req_size);
            db->allocate();
        }

        if(transfer.src_type == ResourceType::BUFFER)
        {
            const auto cmd = cmdpool->begin();
            record_command(cmd, transfer);
            queue->with_cmd_buf(cmd).submit_wait(-1ull);
        }
        else if(transfer.src_type == ResourceType::VECTOR)
        {
            size_t uploaded_sz = 0;
            while(uploaded_sz < transfer.src_range.size)
            {
                auto [pGPU, alloc_sz] = allocator->allocate_best_fit(transfer.src_range.size);
                while(!pGPU)
                {
                    std::tie(pGPU, alloc_sz) = allocator->allocate_best_fit(transfer.src_range.size);
                }
                memcpy(pGPU, static_cast<const std::byte*>(transfer.data) + uploaded_sz, alloc_sz);
                send_to(dbh, {}, { allocator->get_alloc_offset(pGPU), alloc_sz }, transfer.dst_range.offset + uploaded_sz);
                uploaded_sz += alloc_sz;
                allocator->reset();
            }
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
    // auto& r = *RendererVulkan::get_instance();
    // auto& i = r.get_image(image);
    // const auto barrier = generate_image_barrier(image, layout, is_final_layout);
    // const auto dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier });
    // const auto cmd = cmdpool->begin(cmds[0]);
    // vkCmdPipelineBarrier2(cmd, &dep_info);
    // cmdpool->end(cmd);
    // queue->with_cmd_buf(cmd).submit_wait(-1ull); // wait because cmd might be pending on subsequent begins
    // i.current_layout = layout;
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
                           .size = transfer.src_range.size };
        vkCmdCopyBuffer(cmd, sb.buffer, db.buffer, 1, &copy);
    }
    else
    {
        ENG_TODO();
        assert(false);
    }
}

} // namespace gfx
