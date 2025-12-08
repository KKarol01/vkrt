#include "submit_queue.hpp"
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/common/logger.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/common/to_vk.hpp>
#include <eng/common/to_string.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/engine.hpp>

namespace eng
{
namespace gfx
{

void CommandBuffer::barrier(Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                            Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access)
{
    const auto barrier = Vks(VkMemoryBarrier2{ .srcStageMask = to_vk(src_stage),
                                               .srcAccessMask = to_vk(src_access),
                                               .dstStageMask = to_vk(dst_stage),
                                               .dstAccessMask = to_vk(dst_access) });
    const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &barrier });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void CommandBuffer::barrier(Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                            Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access, ImageLayout old_layout,
                            ImageLayout new_layout)
{
    barrier(image, src_stage, src_access, dst_stage, dst_access, old_layout, new_layout,
            ImageSubRange{ { 0, image.mips }, { 0, image.layers } });
    image.current_layout = new_layout;
}

void CommandBuffer::barrier(Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                            Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access, ImageLayout old_layout,
                            ImageLayout new_layout, const ImageSubRange& range)
{
    const auto barr =
        Vks(VkImageMemoryBarrier2{ .srcStageMask = to_vk(src_stage),
                                   .srcAccessMask = to_vk(src_access),
                                   .dstStageMask = to_vk(dst_stage),
                                   .dstAccessMask = to_vk(dst_access),
                                   .oldLayout = to_vk(old_layout),
                                   .newLayout = to_vk(new_layout),
                                   .image = image.md.vk->image,
                                   .subresourceRange = { to_vk(image.deduce_aspect()), range.mips.offset,
                                                         range.mips.size, range.layers.offset, range.layers.size } });
    const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barr });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void CommandBuffer::copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range range)
{
    assert(dst.capacity >= dst_offset + range.size && src.capacity >= range.offset + range.size);
    VkBufferCopy region{ range.offset, dst_offset, range.size };
    vkCmdCopyBuffer(cmd, VkBufferMetadata::get(src).buffer, VkBufferMetadata::get(dst).buffer, 1, &region);
}

void CommandBuffer::copy(Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count)
{
    const auto info = Vks(VkCopyBufferToImageInfo2{ .srcBuffer = VkBufferMetadata::get(src).buffer,
                                                    .dstImage = dst.md.vk->image,
                                                    .dstImageLayout = to_vk(dst.current_layout),
                                                    .regionCount = count,
                                                    .pRegions = regions });
    vkCmdCopyBufferToImage2(cmd, &info);
}

void CommandBuffer::copy(Image& dst, const Image& src, const ImageCopy& copy, ImageLayout dstlayout, ImageLayout srclayout)
{
    auto* dstmd = dst.md.vk;
    const auto* srcmd = dst.md.vk;
    VkImageCopy vkcp{ .srcSubresource = { to_vk(src.deduce_aspect()), copy.srclayers.mip, copy.srclayers.layers.offset,
                                          copy.srclayers.layers.size },
                      .srcOffset = { copy.srcoffset.x, copy.srcoffset.y, copy.srcoffset.z },
                      .dstSubresource = { to_vk(dst.deduce_aspect()), copy.dstlayers.mip, copy.dstlayers.layers.offset,
                                          copy.dstlayers.layers.size },
                      .dstOffset = { copy.dstoffset.x, copy.dstoffset.y, copy.dstoffset.z },
                      .extent = { copy.extent.x, copy.extent.y, copy.extent.z } };
    vkCmdCopyImage(cmd, srcmd->image, to_vk(srclayout), dstmd->image, to_vk(dstlayout), 1, &vkcp);
}

void CommandBuffer::copy(Image& dst, const Image& src)
{
    auto* dstmd = dst.md.vk;
    const auto* srcmd = src.md.vk;
    const auto vkr = VkImageCopy{ .srcSubresource = { to_vk(src.deduce_aspect()), 0, 0, std::min(dst.layers, src.layers) },
                                  .dstSubresource = { to_vk(dst.deduce_aspect()), 0, 0, std::min(dst.layers, src.layers) },
                                  .extent = { dst.width, dst.height, dst.depth } };
    vkCmdCopyImage(cmd, srcmd->image, to_vk(src.current_layout), dstmd->image, to_vk(dst.current_layout), 1, &vkr);
}

void CommandBuffer::blit(Image& dst, const Image& src, const ImageBlit& range, ImageLayout dstlayout,
                         ImageLayout srclayout, ImageFilter filter)
{
    auto* dstmd = dst.md.vk;
    const auto* srcmd = src.md.vk;
    auto blit = VkImageBlit{};
    blit.srcSubresource = { .aspectMask = to_vk(src.deduce_aspect()),
                            .mipLevel = range.srclayers.mip,
                            .baseArrayLayer = range.srclayers.layers.offset,
                            .layerCount = range.srclayers.layers.size };
    blit.srcOffsets[0] = VkOffset3D{ range.srcrange.offset.x, range.srcrange.offset.y, range.srcrange.offset.z };
    blit.srcOffsets[1] =
        VkOffset3D{ range.srcrange.offset.x + range.srcrange.size.x, range.srcrange.offset.y + range.srcrange.size.y,
                    range.srcrange.offset.z + range.srcrange.size.z };
    blit.dstSubresource = { .aspectMask = to_vk(dst.deduce_aspect()),
                            .mipLevel = range.dstlayers.mip,
                            .baseArrayLayer = range.dstlayers.layers.offset,
                            .layerCount = range.dstlayers.layers.size };
    blit.dstOffsets[0] = VkOffset3D{ range.dstrange.offset.x, range.dstrange.offset.y, range.dstrange.offset.z };
    blit.dstOffsets[1] =
        VkOffset3D{ range.dstrange.offset.x + range.dstrange.size.x, range.dstrange.offset.y + range.dstrange.size.y,
                    range.dstrange.offset.z + range.dstrange.size.z };
    vkCmdBlitImage(cmd, srcmd->image, to_vk(srclayout), dstmd->image, to_vk(dstlayout), 1, &blit, to_vk(filter));
}

void CommandBuffer::clear_color(Image& image, ImageLayout layout, Range mips, Range layers, float color)
{
    if(image.current_layout != layout)
    {
        barrier(image, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, image.current_layout, layout);
    }
    const auto clear = VkClearColorValue{ .float32 = color };
    const auto range = VkImageSubresourceRange{ to_vk(image.deduce_aspect()), (uint32_t)mips.offset,
                                                (uint32_t)mips.size, (uint32_t)layers.offset, (uint32_t)layers.size };
    vkCmdClearColorImage(cmd, image.md.vk->image, to_vk(layout), &clear, 1, &range);
}

void CommandBuffer::clear_depth_stencil(Image& image, float clear_depth, uint32_t clear_stencil, ImageLayout layout,
                                        Range mips, Range layers)
{
    if(layout == ImageLayout::UNDEFINED) { layout = image.current_layout; }
    if(image.current_layout != layout)
    {
        barrier(image, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, image.current_layout, layout);
    }
    const auto clear = VkClearDepthStencilValue{ .depth = clear_depth, .stencil = clear_stencil };
    const auto range = VkImageSubresourceRange{ to_vk(image.deduce_aspect()), (uint32_t)mips.offset,
                                                (uint32_t)mips.size, (uint32_t)layers.offset, (uint32_t)layers.size };
    vkCmdClearDepthStencilImage(cmd, image.md.vk->image, to_vk(layout), &clear, 1, &range);
}

void CommandBuffer::bind_index(const Buffer& index, uint32_t offset, VkIndexType type)
{
    vkCmdBindIndexBuffer(cmd, VkBufferMetadata::get(index).buffer, offset, type);
}

void CommandBuffer::bind_pipeline(const Pipeline& pipeline)
{
    const auto& md = *pipeline.md.vk;
    vkCmdBindPipeline(cmd, to_vk(pipeline.type), md.pipeline);
    current_pipeline = &pipeline;
}

void CommandBuffer::bind_descriptors(const DescriptorPool* ps, DescriptorSet* ds, Range32u range)
{
    const auto* md = VkPipelineLayoutMetadata::get(current_pipeline->info.layout.get());
    std::array<VkDescriptorSet, 8> vksets{};
    for(auto i = 0u; i < range.size; ++i)
    {
        vksets.at(i) = VkDescriptorSetMetadata::get(ds[i])->set;
    }
    vkCmdBindDescriptorSets(cmd, to_vk(current_pipeline->type), md->layout, range.offset, range.size, vksets.data(), 0, nullptr);
}

void CommandBuffer::push_constants(Flags<ShaderStage> stages, const void* const values, Range32u range)
{
    memcpy(pcbuf + range.offset, values, range.size);
    flush_pc_size = std::max(flush_pc_size, range.offset + range.size);
}

void CommandBuffer::bind_resource(uint32_t slot, Handle<Buffer> resource, Range range)
{
    const auto idx = Engine::get().renderer->get_bindless(resource, range);
    push_constants(ShaderStage::ALL, &idx, { slot * (uint32_t)sizeof(idx), sizeof(idx) });
}

void CommandBuffer::bind_resource(uint32_t slot, Handle<Texture> resource)
{
    const auto idx = Engine::get().renderer->get_bindless(resource);
    push_constants(ShaderStage::ALL, &idx, { slot * (uint32_t)sizeof(idx), sizeof(idx) });
}

void CommandBuffer::bind_resource(uint32_t slot, Handle<Sampler> resource)
{
    const auto idx = Engine::get().renderer->get_bindless(resource);
    push_constants(ShaderStage::ALL, &idx, { slot * (uint32_t)sizeof(idx), sizeof(idx) });
}

void CommandBuffer::set_viewports(const VkViewport* viewports, uint32_t count)
{
    vkCmdSetViewportWithCount(cmd, count, viewports);
}

void CommandBuffer::set_scissors(const VkRect2D* scissors, uint32_t count)
{
    vkCmdSetScissorWithCount(cmd, count, scissors);
}

void CommandBuffer::begin_rendering(const VkRenderingInfo& info)
{
    before_draw_dispatch(); // todo: not sure if this should be the only place for non compute/rt dispatches
    vkCmdBeginRendering(cmd, &info);
}

void CommandBuffer::end_rendering() { vkCmdEndRendering(cmd); }

void CommandBuffer::before_draw_dispatch()
{
    if(flush_pc_size > 0)
    {
        const auto* md = VkPipelineLayoutMetadata::get(current_pipeline->info.layout.get());
        vkCmdPushConstants(cmd, md->layout, gfx::to_vk(Flags<ShaderStage>{ ShaderStage::ALL }), 0u, flush_pc_size, pcbuf);
        flush_pc_size = 0;
        auto* r = Engine::get().renderer;
        // todo: don't do this each time; only when pipeline layouts disturb it and when it wasn't already bound.
        r->bindless->bind(this);
    }
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset,
                                 uint32_t vertex_offset, uint32_t instance_offset)
{
    before_draw_dispatch();
    vkCmdDrawIndexed(cmd, index_count, instance_count, index_offset, vertex_offset, instance_offset);
}

void CommandBuffer::draw_indexed_indirect_count(const Buffer& indirect, size_t indirect_offset, const Buffer& count,
                                                size_t count_offset, uint32_t max_draw_count, uint32_t stride)
{
    before_draw_dispatch();
    vkCmdDrawIndexedIndirectCount(cmd, VkBufferMetadata::get(indirect).buffer, indirect_offset,
                                  VkBufferMetadata::get(count).buffer, count_offset, max_draw_count, stride);
}

void CommandBuffer::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    before_draw_dispatch();
    vkCmdDispatch(cmd, x, y, z);
}

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
        VK_CHECK(vkCreateFence(RendererBackendVulkan::get_instance()->dev, &vkinfo, nullptr, &fence));
        if(name.size()) { set_debug_name(fence, name); }
    }
    else
    {
        auto vkinfo = Vks(VkSemaphoreCreateInfo{});
        const auto timeline_info =
            Vks(VkSemaphoreTypeCreateInfo{ .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = value });
        if(type == TIMELINE_SEMAPHORE) { vkinfo.pNext = &timeline_info; }
        VK_CHECK(vkCreateSemaphore(RendererBackendVulkan::get_instance()->dev, &vkinfo, nullptr, &semaphore));
        if(name.size()) { set_debug_name(semaphore, name); }
    }
}

void Sync::destroy()
{
    if(type == UNKNOWN) { return; }
    if(type == FENCE) { vkDestroyFence(RendererBackendVulkan::get_instance()->dev, fence, nullptr); }
    else { vkDestroySemaphore(RendererBackendVulkan::get_instance()->dev, semaphore, nullptr); }
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
        vkSignalSemaphore(RendererBackendVulkan::get_instance()->dev, &info);
    }
    else
    {
        ENG_ERROR("Sync object of type {} cannot be signaled on.", eng::to_string(type));
        return;
    }
    this->value = value;
}

VkResult Sync::wait_cpu(size_t timeout, uint64_t value) const
{
    if(type == UNKNOWN)
    {
        ENG_ERROR("Sync object was not initialized.");
        return VK_ERROR_UNKNOWN;
    }
    if(value == ~0ull) { value = this->value; }
    if(type == FENCE) { return vkWaitForFences(RendererBackendVulkan::get_instance()->dev, 1, &fence, true, timeout); }
    else if(type == TIMELINE_SEMAPHORE)
    {
        const auto info = Vks(VkSemaphoreWaitInfo{ .semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value });
        return vkWaitSemaphores(RendererBackendVulkan::get_instance()->dev, &info, timeout);
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
    if(type == FENCE) { vkResetFences(RendererBackendVulkan::get_instance()->dev, 1, &fence); }
    else
    {
        auto type = this->type;
        auto name = this->name;
        destroy();
        init({ type, value, name });
    }
}

SubmitQueue::SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept
    : dev(dev), queue(queue), family_idx(family_idx),
      fence(RendererBackendVulkan::get_instance()->make_sync({ SyncType::FENCE }))
{
}

CommandPool* SubmitQueue::make_command_pool(VkCommandPoolCreateFlags flags)
{
    return &command_pools.emplace_back(dev, family_idx, flags);
}

SubmitQueue& SubmitQueue::wait_sync(Sync* sync, Flags<PipelineStage> stages, uint64_t value)
{
    assert(sync);
    if(sync->type == SyncType::BINARY_SEMAPHORE || sync->type == SyncType::TIMELINE_SEMAPHORE)
    {
        submission.wait_sems.push_back(sync);
        submission.wait_values.push_back(sync->wait_gpu(value));
        submission.wait_stages.push_back(stages);
    }
    return *this;
}

SubmitQueue& SubmitQueue::signal_sync(Sync* sync, Flags<PipelineStage> stages, uint64_t value)
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
                                                  .stageMask = to_vk(submission.wait_stages[i]) });
    }
    for(auto i = 0u; i < sig_sems.size(); ++i)
    {
        sig_sems[i] = Vks(VkSemaphoreSubmitInfo{ .semaphore = submission.signal_sems[i]->semaphore,
                                                 .value = submission.signal_values[i],
                                                 .stageMask = to_vk(submission.signal_stages[i]) });
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
                                             .pSwapchains = &VkSwapchainMetadata::get(*swapchain).swapchain,
                                             .pImageIndices = &swapchain->current_index });
    vkQueuePresentKHR(queue, &pinfo);
    submission = {};
}

void SubmitQueue::wait_idle() { vkQueueWaitIdle(queue); }

} // namespace gfx
} // namespace eng
