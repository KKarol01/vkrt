#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/common/logger.hpp>
#include <eng/renderer/resources/resources.hpp>
#include <eng/renderer/renderer_vulkan.hpp>

namespace gfx
{

void CommandBuffer::barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                            VkAccessFlags2 dst_access)
{
    const auto barrier = Vks(VkMemoryBarrier2{
        .srcStageMask = src_stage, .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access });
    const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &barrier });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void CommandBuffer::barrier(Image& image, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                            VkAccessFlags2 dst_access, VkImageLayout old_layout, VkImageLayout new_layout)
{
    const auto barr = Vks(VkImageMemoryBarrier2{ .srcStageMask = src_stage,
                                                 .srcAccessMask = src_access,
                                                 .dstStageMask = dst_stage,
                                                 .dstAccessMask = dst_access,
                                                 .oldLayout = old_layout,
                                                 .newLayout = new_layout,
                                                 .image = image.image,
                                                 .subresourceRange = { image.deduce_aspect(), 0, image.mips, 0, image.layers } });
    const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barr });
    image.current_layout = new_layout;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void CommandBuffer::copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range range)
{
    VkBufferCopy region{ range.offset, dst_offset, range.size };
    vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &region);
}

void CommandBuffer::copy(Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count)
{
    const auto info = Vks(VkCopyBufferToImageInfo2{
        .srcBuffer = src.buffer, .dstImage = dst.image, .dstImageLayout = dst.current_layout, .regionCount = count, .pRegions = regions });
    vkCmdCopyBufferToImage2(cmd, &info);
}

void CommandBuffer::clear_color(Image& image, VkImageLayout layout, Range mips, Range layers, float color)
{
    if(image.current_layout != layout)
    {
        barrier(image, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, image.current_layout, layout);
    }
    const auto clear = VkClearColorValue{ .float32 = color };
    const auto range = VkImageSubresourceRange{ image.deduce_aspect(), (uint32_t)mips.offset, (uint32_t)mips.size,
                                                (uint32_t)layers.offset, (uint32_t)layers.size };
    vkCmdClearColorImage(cmd, image.image, layout, &clear, 1, &range);
}

void CommandBuffer::clear_depth_stencil(Image& image, VkImageLayout layout, Range mips, Range layers, float clear_depth, uint32_t clear_stencil)
{
    if(image.current_layout != layout)
    {
        barrier(image, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, image.current_layout, layout);
    }
    const auto clear = VkClearDepthStencilValue{ .depth = 0.0f, .stencil = 0 };
    const auto range = VkImageSubresourceRange{ image.deduce_aspect(), (uint32_t)mips.offset, (uint32_t)mips.size,
                                                (uint32_t)layers.offset, (uint32_t)layers.size };
    vkCmdClearDepthStencilImage(cmd, image.image, layout, &clear, 1, &range);
}

void CommandBuffer::bind_index(Buffer& index, uint32_t offset, VkIndexType type)
{
    vkCmdBindIndexBuffer(cmd, index.buffer, offset, type);
}

void CommandBuffer::bind_pipeline(const Pipeline& pipeline)
{
    const auto* md = pipeline.vkmetadata;
    vkCmdBindPipeline(cmd, md->bind_point, md->pipeline);
    current_pipeline = &pipeline;
}

void CommandBuffer::bind_descriptors(VkDescriptorSet* sets, Range range)
{
    vkCmdBindDescriptorSets(cmd, current_pipeline->vkmetadata->bind_point, current_pipeline->vkmetadata->layout,
                            (uint32_t)range.offset, (uint32_t)range.size, sets, 0, nullptr);
}

void CommandBuffer::push_constants(VkShaderStageFlags stages, const void* const values, Range range)
{
    vkCmdPushConstants(cmd, current_pipeline->vkmetadata->layout, stages, (uint32_t)range.offset, (uint32_t)range.size, values);
}

void CommandBuffer::set_viewports(const VkViewport* viewports, uint32_t count)
{
    vkCmdSetViewportWithCount(cmd, count, viewports);
}

void CommandBuffer::set_scissors(const VkRect2D* scissors, uint32_t count)
{
    vkCmdSetScissorWithCount(cmd, count, scissors);
}

void CommandBuffer::begin_rendering(const VkRenderingInfo& info) { vkCmdBeginRendering(cmd, &info); }

void CommandBuffer::end_rendering() { vkCmdEndRendering(cmd); }

void CommandBuffer::draw_indexed_indirect_count(Buffer& indirect, size_t indirect_offset, Buffer& count,
                                                size_t count_offset, uint32_t max_draw_count, uint32_t stride)
{
    vkCmdDrawIndexedIndirectCount(cmd, indirect.buffer, indirect_offset, count.buffer, count_offset, max_draw_count, stride);
}

void CommandBuffer::dispatch(uint32_t x, uint32_t y, uint32_t z) { vkCmdDispatch(cmd, x, y, z); }

CommandPool::CommandPool(VkDevice dev, uint32_t family_index, VkCommandPoolCreateFlags flags) noexcept : dev(dev)
{
    const auto vk_info = Vks(VkCommandPoolCreateInfo{ .flags = flags, .queueFamilyIndex = family_index });
    VK_CHECK(vkCreateCommandPool(dev, &vk_info, nullptr, &pool));
}

CommandBuffer* CommandPool::allocate()
{
    if(free.empty())
    {
        CommandBuffer* cmd = &used.emplace_back();
        const auto vk_info = Vks(VkCommandBufferAllocateInfo{
            .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 });
        VK_CHECK(vkAllocateCommandBuffers(dev, &vk_info, &cmd->cmd));
        return cmd;
    }
    auto cmd = &used.emplace_back(free.front());
    free.pop_front();
    return cmd;
}

CommandBuffer* CommandPool::begin()
{
    auto cmd = allocate();
    begin(cmd);
    return cmd;
}

CommandBuffer* CommandPool::begin(CommandBuffer* cmd)
{
    const auto vk_info = Vks(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    VK_CHECK(vkBeginCommandBuffer(cmd->cmd, &vk_info));
    return cmd;
}

void CommandPool::reset(CommandBuffer* cmd) { vkResetCommandBuffer(cmd->cmd, {}); }

void CommandPool::end(CommandBuffer* cmd) { vkEndCommandBuffer(cmd->cmd); }

void CommandPool::reset()
{
    VK_CHECK(vkResetCommandPool(dev, pool, {}));
    free = std::move(used);
}

SubmitQueue::SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept
    : dev(dev), queue(queue), family_idx(family_idx)
{
}

CommandPool* SubmitQueue::make_command_pool(VkCommandPoolCreateFlags flags)
{
    return &command_pools.emplace_back(dev, family_idx, flags);
}

VkFence SubmitQueue::make_fence(bool signaled)
{
    const auto vk_info = Vks(VkFenceCreateInfo{ .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{} });
    auto& fence = fences.emplace_back();
    VK_CHECK(vkCreateFence(dev, &vk_info, nullptr, &fence));
    return fence;
}

VkSemaphore SubmitQueue::make_semaphore()
{
    const auto vk_info = Vks(VkSemaphoreCreateInfo{});
    auto& sem = semaphores.emplace_back();
    VK_CHECK(vkCreateSemaphore(dev, &vk_info, nullptr, &sem));
    return sem;
}

void SubmitQueue::reset_fence(VkFence fence) { VK_CHECK(vkResetFences(dev, 1, &fence)); }

void SubmitQueue::destroy_fence(VkFence fence)
{
    if(fence)
    {
        auto it = std::find(fences.begin(), fences.end(), fence);
        assert(it != fences.end());
        if(it != fences.end())
        {
            fences.erase(it);
            vkDestroyFence(dev, fence, nullptr);
        }
    }
}

VkResult SubmitQueue::wait_fence(VkFence fence, uint64_t timeout)
{
    return vkWaitForFences(dev, 1, &fence, true, timeout);
}

SubmitQueue& SubmitQueue::with_fence(VkFence fence)
{
    if(submission.fence) { ENG_WARN("Overwriting already defined fence in submission"); }
    submission.fence = fence;
    return *this;
}

SubmitQueue& SubmitQueue::with_wait_sem(VkSemaphore sem, VkPipelineStageFlags2 stages)
{
    submission.wait_sems.push_back(Vks(VkSemaphoreSubmitInfo{ .semaphore = sem, .stageMask = stages }));
    return *this;
}

SubmitQueue& SubmitQueue::with_sig_sem(VkSemaphore sem, VkPipelineStageFlags2 stages)
{
    submission.sig_sems.push_back(Vks(VkSemaphoreSubmitInfo{ .semaphore = sem, .stageMask = stages }));
    return *this;
}

SubmitQueue& SubmitQueue::with_cmd_buf(CommandBuffer* cmd)
{
    submission.cmds.push_back(Vks(VkCommandBufferSubmitInfo{ .commandBuffer = cmd->cmd }));
    return *this;
}

VkResult SubmitQueue::submit()
{
    const auto vk_info = Vks(VkSubmitInfo2{
        .waitSemaphoreInfoCount = (uint32_t)submission.wait_sems.size(),
        .pWaitSemaphoreInfos = submission.wait_sems.data(),
        .commandBufferInfoCount = (uint32_t)submission.cmds.size(),
        .pCommandBufferInfos = submission.cmds.data(),
        .signalSemaphoreInfoCount = (uint32_t)submission.sig_sems.size(),
        .pSignalSemaphoreInfos = submission.sig_sems.data(),
    });
    const auto sres = vkQueueSubmit2(queue, 1, &vk_info, submission.fence);
    VK_CHECK(sres);
    submission = Submission{};
    return sres;
}

VkResult SubmitQueue::submit_wait(uint64_t timeout)
{
    VkFence fence{};
    bool is_fence_temp = false;
    if(submission.fence) { fence = submission.fence; }
    else
    {
        fence = make_fence(false);
        submission.fence = fence;
        is_fence_temp = true;
    }
    assert(fence);
    if(!fence) { return VK_ERROR_DEVICE_LOST; }
    const auto sres = submit();
    const auto res = wait_fence(fence, timeout);
    if(is_fence_temp) { destroy_fence(fence); }
    if(res == VK_SUCCESS) { return sres; }
    return res;
}

void SubmitQueue::wait_idle() { vkQueueWaitIdle(queue); }

} // namespace gfx
