#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/common/logger.hpp>
#include <eng/renderer/resources/resources.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/common/to_string.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/engine.hpp>

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

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset,
                                 uint32_t vertex_offset, uint32_t instance_offset)
{
    vkCmdDrawIndexed(cmd, index_count, instance_count, index_offset, vertex_offset, instance_offset);
}

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

void Sync::init(const SyncCreateInfo& info)
{
    if(type != UNKNOWN)
    {
        ENG_ERROR("Trying to init already created Sync object");
        return;
    }
    type = info.type;
    value = info.value;
    name = info.name;
    if(type == FENCE)
    {
        const auto vkinfo = Vks(VkFenceCreateInfo{ .flags = value > 0 ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{} });
        VK_CHECK(vkCreateFence(RendererVulkan::get_instance()->dev, &vkinfo, nullptr, &fence));
        if(name.size()) { set_debug_name(fence, name); }
    }
    else
    {
        auto vkinfo = Vks(VkSemaphoreCreateInfo{});
        const auto timeline_info =
            Vks(VkSemaphoreTypeCreateInfo{ .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = value });
        if(type == TIMELINE_SEMAPHORE) { vkinfo.pNext = &timeline_info; }
        VK_CHECK(vkCreateSemaphore(RendererVulkan::get_instance()->dev, &vkinfo, nullptr, &semaphore));
        if(name.size()) { set_debug_name(semaphore, name); }
    }
}

void Sync::destroy()
{
    if(type == UNKNOWN) { return; }
    if(type == FENCE) { vkDestroyFence(RendererVulkan::get_instance()->dev, fence, nullptr); }
    else { vkDestroySemaphore(RendererVulkan::get_instance()->dev, semaphore, nullptr); }
    type = UNKNOWN;
    value = 0u;
    name.clear();
}

void Sync::signal_cpu(uint64_t value)
{
    if(value == ~0ull) { value = this->value + 1; }
    if(type == TIMELINE_SEMAPHORE)
    {
        const auto info = Vks(VkSemaphoreSignalInfo{ .semaphore = semaphore, .value = value });
        vkSignalSemaphore(RendererVulkan::get_instance()->dev, &info);
    }
    else
    {
        ENG_ERROR("Sync object of type {} cannot be signaled on.", eng::to_string(type));
        return;
    }
    this->value = value;
}

VkResult Sync::wait_cpu(size_t timeout, uint64_t value)
{
    if(type == UNKNOWN)
    {
        ENG_ERROR("Sync object was not initialized.");
        return VK_ERROR_UNKNOWN;
    }
    if(value == ~0ull) { value = this->value; }
    if(type == FENCE) { return vkWaitForFences(RendererVulkan::get_instance()->dev, 1, &fence, true, timeout); }
    else if(type == TIMELINE_SEMAPHORE)
    {
        const auto info = Vks(VkSemaphoreWaitInfo{ .semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value });
        return vkWaitSemaphores(RendererVulkan::get_instance()->dev, &info, timeout);
    }
    ENG_ERROR("Sync object of type {} cannot be waited on.", eng::to_string(type));
    return VK_ERROR_UNKNOWN;
}

uint64_t Sync::signal_gpu(uint64_t value)
{
    this->value = value == ~0ull ? (this->value + 1) : value;
    return this->value;
}

uint64_t Sync::wait_gpu(uint64_t value)
{
    const auto val = value == ~0ull ? this->value : value;
    if(type == BINARY_SEMAPHORE) { this->value = 0; }
    return val;
}

void Sync::reset(uint64_t value)
{
    if(type == UNKNOWN) { return; }
    if(type == FENCE) { vkResetFences(RendererVulkan::get_instance()->dev, 1, &fence); }
    else
    {
        auto type = this->type;
        auto name = this->name;
        destroy();
        init({ type, value, name });
    }
}

SubmitQueue::SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept
    : dev(dev), queue(queue), family_idx(family_idx), fence(RendererVulkan::get_instance()->make_sync({ SyncType::FENCE }))
{
}

CommandPool* SubmitQueue::make_command_pool(VkCommandPoolCreateFlags flags)
{
    return &command_pools.emplace_back(dev, family_idx, flags);
}

SubmitQueue& SubmitQueue::wait_sync(Sync* sync, VkPipelineStageFlags2 stages, uint64_t value)
{
    if(sync->type == SyncType::BINARY_SEMAPHORE || sync->type == SyncType::TIMELINE_SEMAPHORE)
    {
        submission.wait_sems.push_back(sync);
        submission.wait_values.push_back(sync->wait_gpu(value));
        submission.wait_stages.push_back(stages);
    }
    return *this;
}

SubmitQueue& SubmitQueue::signal_sync(Sync* sync, VkPipelineStageFlags2 stages, uint64_t value)
{
    if(sync->type == SyncType::FENCE)
    {
        assert(!submission.fence);
        submission.fence = sync;
    }
    else if(sync->type == SyncType::BINARY_SEMAPHORE || sync->type == SyncType::TIMELINE_SEMAPHORE)
    {
        submission.signal_sems.push_back(sync);
        submission.signal_values.push_back(sync->signal_gpu(value));
        submission.signal_stages.push_back(stages);
    }
    return *this;
}

SubmitQueue& SubmitQueue::with_cmd_buf(CommandBuffer* cmd)
{
    submission.cmds.push_back(cmd);
    return *this;
}

VkResult SubmitQueue::submit()
{
    std::vector<VkSemaphoreSubmitInfo> wait_sems(submission.wait_sems.size());
    std::vector<VkSemaphoreSubmitInfo> sig_sems(submission.signal_sems.size());
    std::vector<VkCommandBufferSubmitInfo> cmds(submission.cmds.size());

    for(auto i = 0u; i < wait_sems.size(); ++i)
    {
        wait_sems[i] = Vks(VkSemaphoreSubmitInfo{ .semaphore = submission.wait_sems[i]->semaphore,
                                                  .value = submission.wait_values[i],
                                                  .stageMask = submission.wait_stages[i] });
    }
    for(auto i = 0u; i < sig_sems.size(); ++i)
    {
        sig_sems[i] = Vks(VkSemaphoreSubmitInfo{ .semaphore = submission.signal_sems[i]->semaphore,
                                                 .value = submission.signal_values[i],
                                                 .stageMask = submission.signal_stages[i] });
    }
    for(auto i = 0u; i < cmds.size(); ++i)
    {
        cmds[i] = Vks(VkCommandBufferSubmitInfo{ .commandBuffer = submission.cmds[i]->cmd });
    }

    const auto vk_info = Vks(VkSubmitInfo2{
        .waitSemaphoreInfoCount = (uint32_t)wait_sems.size(),
        .pWaitSemaphoreInfos = wait_sems.data(),
        .commandBufferInfoCount = (uint32_t)cmds.size(),
        .pCommandBufferInfos = cmds.data(),
        .signalSemaphoreInfoCount = (uint32_t)sig_sems.size(),
        .pSignalSemaphoreInfos = sig_sems.data(),
    });
    const auto sres = vkQueueSubmit2(queue, 1, &vk_info, submission.fence ? submission.fence->fence : nullptr);
    VK_CHECK(sres);
    submission = Submission{};
    return sres;
}

VkResult SubmitQueue::submit_wait(uint64_t timeout)
{
    bool is_fence_temp = false;
    Sync* sfence{};
    if(!submission.fence)
    {
        sfence = fence;
        signal_sync(fence);
        is_fence_temp = true;
    }
    else { sfence = submission.fence; }
    assert(sfence);
    const auto sres = submit();
    const auto res = sfence->wait_cpu(timeout);
    if(is_fence_temp) { sfence->reset(); }
    return res;
}

void SubmitQueue::present(Swapchain* swapchain)
{
    std::vector<VkSemaphore> wait_sems(submission.wait_sems.size());
    for(auto i = 0u; i < wait_sems.size(); ++i)
    {
        wait_sems[i] = submission.wait_sems[i]->semaphore;
    }

    const auto pinfo = Vks(VkPresentInfoKHR{ .waitSemaphoreCount = (uint32_t)wait_sems.size(),
                                             .pWaitSemaphores = wait_sems.data(),
                                             .swapchainCount = 1,
                                             .pSwapchains = &swapchain->swapchain,
                                             .pImageIndices = &swapchain->current_index });
    vkQueuePresentKHR(queue, &pinfo);
    submission = {};
}

void SubmitQueue::wait_idle() { vkQueueWaitIdle(queue); }

} // namespace gfx
