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

void CommandBufferVk::barrier(Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                              Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access)
{
    const auto barrier = Vks(VkMemoryBarrier2{ .srcStageMask = to_vk(src_stage),
                                               .srcAccessMask = to_vk(src_access),
                                               .dstStageMask = to_vk(dst_stage),
                                               .dstAccessMask = to_vk(dst_access) });
    const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &barrier });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void CommandBufferVk::barrier(const Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                              Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access, ImageLayout old_layout,
                              ImageLayout new_layout, const ImageMipLayerRange& range)
{
    const auto barr =
        Vks(VkImageMemoryBarrier2{ .srcStageMask = to_vk(src_stage),
                                   .srcAccessMask = to_vk(src_access),
                                   .dstStageMask = to_vk(dst_stage),
                                   .dstAccessMask = to_vk(dst_access),
                                   .oldLayout = to_vk(old_layout),
                                   .newLayout = to_vk(new_layout),
                                   .image = image.md.as_vk()->image,
                                   .subresourceRange = { to_vk(get_aspect_from_format(image.format)), range.mips.offset,
                                                         range.mips.size, range.layers.offset, range.layers.size } });
    const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barr });
    vkCmdPipelineBarrier2(cmd, &dep);
}

void CommandBufferVk::copy(const Buffer& dst, const Buffer& src, size_t dst_offset, Range range)
{
    ENG_ASSERT(dst_offset + range.size <= dst.capacity && range.offset + range.size <= src.capacity);
    VkBufferCopy region{ range.offset, dst_offset, range.size };
    vkCmdCopyBuffer(cmd, src.md.as_vk()->buffer, dst.md.as_vk()->buffer, 1, &region);
}

void CommandBufferVk::copy(const Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count)
{
    const auto info = Vks(VkCopyBufferToImageInfo2{ .srcBuffer = src.md.as_vk()->buffer,
                                                    .dstImage = dst.md.as_vk()->image,
                                                    .dstImageLayout = to_vk(ImageLayout::TRANSFER_DST),
                                                    .regionCount = count,
                                                    .pRegions = regions });
    vkCmdCopyBufferToImage2(cmd, &info);
}

void CommandBufferVk::copy(const Image& dst, const Image& src, const ImageCopy& copy)
{
    auto* dstmd = dst.md.as_vk();
    const auto* srcmd = dst.md.as_vk();
    VkImageCopy vkcp{ .srcSubresource = { to_vk(get_aspect_from_format(src.format)), copy.srclayers.mip,
                                          copy.srclayers.layers.offset, copy.srclayers.layers.size },
                      .srcOffset = { copy.srcoffset.x, copy.srcoffset.y, copy.srcoffset.z },
                      .dstSubresource = { to_vk(get_aspect_from_format(dst.format)), copy.dstlayers.mip,
                                          copy.dstlayers.layers.offset, copy.dstlayers.layers.size },
                      .dstOffset = { copy.dstoffset.x, copy.dstoffset.y, copy.dstoffset.z },
                      .extent = { copy.extent.x, copy.extent.y, copy.extent.z } };
    vkCmdCopyImage(cmd, srcmd->image, to_vk(ImageLayout::TRANSFER_SRC), dstmd->image, to_vk(ImageLayout::TRANSFER_DST), 1, &vkcp);
}

void CommandBufferVk::copy(const Image& dst, const Image& src)
{
    auto* dstmd = dst.md.as_vk();
    const auto* srcmd = src.md.as_vk();
    const auto vkr =
        VkImageCopy{ .srcSubresource = { to_vk(get_aspect_from_format(src.format)), 0, 0, std::min(dst.layers, src.layers) },
                     .dstSubresource = { to_vk(get_aspect_from_format(dst.format)), 0, 0, std::min(dst.layers, src.layers) },
                     .extent = { dst.width, dst.height, dst.depth } };
    vkCmdCopyImage(cmd, srcmd->image, to_vk(ImageLayout::TRANSFER_SRC), dstmd->image, to_vk(ImageLayout::TRANSFER_DST), 1, &vkr);
}

void CommandBufferVk::blit(const Image& dst, const Image& src, const ImageBlit& blit)
{
    auto* dstmd = dst.md.as_vk();
    const auto* srcmd = src.md.as_vk();
    auto vkblit = VkImageBlit{};
    vkblit.srcSubresource = { .aspectMask = to_vk(get_aspect_from_format(src.format)),
                              .mipLevel = blit.srclayers.mip,
                              .baseArrayLayer = blit.srclayers.layers.offset,
                              .layerCount = blit.srclayers.layers.size };
    vkblit.srcOffsets[0] = VkOffset3D{ blit.srcrange.offset.x, blit.srcrange.offset.y, blit.srcrange.offset.z };
    vkblit.srcOffsets[1] =
        VkOffset3D{ blit.srcrange.offset.x + blit.srcrange.size.x, blit.srcrange.offset.y + blit.srcrange.size.y,
                    blit.srcrange.offset.z + blit.srcrange.size.z };
    vkblit.dstSubresource = { .aspectMask = to_vk(get_aspect_from_format(dst.format)),
                              .mipLevel = blit.dstlayers.mip,
                              .baseArrayLayer = blit.dstlayers.layers.offset,
                              .layerCount = blit.dstlayers.layers.size };
    vkblit.dstOffsets[0] = VkOffset3D{ blit.dstrange.offset.x, blit.dstrange.offset.y, blit.dstrange.offset.z };
    vkblit.dstOffsets[1] =
        VkOffset3D{ blit.dstrange.offset.x + blit.dstrange.size.x, blit.dstrange.offset.y + blit.dstrange.size.y,
                    blit.dstrange.offset.z + blit.dstrange.size.z };
    vkCmdBlitImage(cmd, srcmd->image, to_vk(ImageLayout::TRANSFER_SRC), dstmd->image, to_vk(ImageLayout::TRANSFER_DST),
                   1, &vkblit, to_vk(blit.filter));
}

void CommandBufferVk::clear_color(const Image& image, const Color4f& color)
{
    const auto clear = VkClearColorValue{ .float32 = { color.x, color.y, color.z, color.a } };
    const auto range =
        VkImageSubresourceRange{ to_vk(ImageAspect::COLOR), 0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS };
    vkCmdClearColorImage(cmd, image.md.as_vk()->image, to_vk(ImageLayout::TRANSFER_DST), &clear, 1, &range);
}

void CommandBufferVk::clear_depth_stencil(const Image& image, float clear_depth, std::optional<uint32_t> clear_stencil)
{
    const auto clear = VkClearDepthStencilValue{ .depth = clear_depth, .stencil = clear_stencil ? 0 : *clear_stencil };
    const auto range = VkImageSubresourceRange{ to_vk(clear_stencil ? ImageAspect::DEPTH_STENCIL : ImageAspect::DEPTH),
                                                0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS };
    vkCmdClearDepthStencilImage(cmd, image.md.as_vk()->image, to_vk(ImageLayout::TRANSFER_DST), &clear, 1, &range);
}

void CommandBufferVk::bind_index(const Buffer& index, uint32_t offset, VkIndexType type)
{
    vkCmdBindIndexBuffer(cmd, index.md.as_vk()->buffer, offset, type);
}

void CommandBufferVk::bind_pipeline(const Pipeline& pipeline)
{
    const auto& md = *pipeline.md.vk;
    vkCmdBindPipeline(cmd, to_vk(pipeline.type), md.pipeline);
    current_pipeline = &pipeline;
    rebind_desc_sets = true;
}

void CommandBufferVk::bind_sets(const void* sets, uint32_t count)
{
    if(!sets)
    {
        ENG_ASSERT(false);
        return;
    }
    const DescriptorSetVk* vksets = (const DescriptorSetVk*)sets;
    for(auto i = 0u; i < count; ++i)
    {
        vkCmdBindDescriptorSets(cmd, to_vk(current_pipeline->type), current_pipeline->info.layout->md.vk->layout,
                                vksets[i].setidx, 1, &vksets[i].set, 0, nullptr);
    }
}

void CommandBufferVk::bind_resources(uint32_t slot, std::span<DescriptorResource> resources)
{
    ENG_ASSERT(current_pipeline && current_pipeline->info.layout);
    rebind_desc_sets = true;
    descriptor_allocator->bind_set(slot, resources, current_pipeline->info.layout.get());
}

void CommandBufferVk::push_constants(Flags<ShaderStage> stages, const void* const values, Range32u range)
{
    vkCmdPushConstants(cmd, current_pipeline->info.layout->md.vk->layout, VK_SHADER_STAGE_ALL, range.offset, range.size, values);
}

void CommandBufferVk::set_viewports(const VkViewport* viewports, uint32_t count)
{
    vkCmdSetViewportWithCount(cmd, count, viewports);
}

void CommandBufferVk::set_scissors(const VkRect2D* scissors, uint32_t count)
{
    vkCmdSetScissorWithCount(cmd, count, scissors);
}

void CommandBufferVk::begin_rendering(const VkRenderingInfo& info)
{
    before_draw_dispatch(); // todo: not sure if this should be the only place for non compute/rt dispatches
    vkCmdBeginRendering(cmd, &info);
}

void CommandBufferVk::end_rendering() { vkCmdEndRendering(cmd); }

void CommandBufferVk::before_draw_dispatch()
{
    if(rebind_desc_sets)
    {
        descriptor_allocator->flush(this);
        rebind_desc_sets = false;
    }
}

void CommandBufferVk::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t vertex_offset, uint32_t instance_offset)
{
    before_draw_dispatch();
    vkCmdDraw(cmd, vertex_count, instance_count, vertex_offset, instance_offset);
}

void CommandBufferVk::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset,
                                   uint32_t vertex_offset, uint32_t instance_offset)
{
    before_draw_dispatch();
    vkCmdDrawIndexed(cmd, index_count, instance_count, index_offset, vertex_offset, instance_offset);
}

void CommandBufferVk::draw_indexed_indirect_count(const Buffer& indirect, size_t indirect_offset, const Buffer& count,
                                                  size_t count_offset, uint32_t max_draw_count, uint32_t stride)
{
    before_draw_dispatch();
    vkCmdDrawIndexedIndirectCount(cmd, indirect.md.as_vk()->buffer, indirect_offset, count.md.as_vk()->buffer,
                                  count_offset, max_draw_count, stride);
}

void CommandBufferVk::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    before_draw_dispatch();
    vkCmdDispatch(cmd, x, y, z);
}

void CommandBufferVk::begin_label(const std::string& label)
{
#ifndef NDEBUG
    const auto vkl = Vks(VkDebugUtilsLabelEXT{ .pLabelName = label.c_str(), .color = { 0.0f, 0.0f, 1.0f, 1.0f } });
    vkCmdBeginDebugUtilsLabelEXT(cmd, &vkl);
#endif
}

void CommandBufferVk::end_label()
{
#ifndef NDEBUG
    vkCmdEndDebugUtilsLabelEXT(cmd);
#endif
}

CommandPoolVk::CommandPoolVk(VkDevice dev, uint32_t family_index, VkCommandPoolCreateFlags flags) noexcept : dev(dev)
{
    const auto vk_info = Vks(VkCommandPoolCreateInfo{ .flags = flags, .queueFamilyIndex = family_index });
    VK_CHECK(vkCreateCommandPool(dev, &vk_info, nullptr, &pool));
}

ICommandBuffer* CommandPoolVk::allocate()
{
    if(free.empty())
    {
        const auto vk_info = Vks(VkCommandBufferAllocateInfo{
            .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 });
        VkCommandBuffer vkcmd;
        VK_CHECK(vkAllocateCommandBuffers(dev, &vk_info, &vkcmd));
        return &used.emplace_back(vkcmd, get_renderer().descriptor_allocator);
    }
    auto* cmd = &used.emplace_back(std::move(free.front()));
    free.pop_front();
    return cmd;
}

ICommandBuffer* CommandPoolVk::begin()
{
    auto cmd = allocate();
    begin(cmd);
    return cmd;
}

ICommandBuffer* CommandPoolVk::begin(ICommandBuffer* cmd)
{
    const auto vk_info = Vks(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    VK_CHECK(vkBeginCommandBuffer(((CommandBufferVk*)cmd)->cmd, &vk_info));
    return cmd;
}

void CommandPoolVk::reset(ICommandBuffer* cmd) { vkResetCommandBuffer(((CommandBufferVk*)cmd)->cmd, {}); }

void CommandPoolVk::end(ICommandBuffer* cmd) { vkEndCommandBuffer(((CommandBufferVk*)cmd)->cmd); }

void CommandPoolVk::reset()
{
    VK_CHECK(vkResetCommandPool(dev, pool, {}));
    while(used.size())
    {
        free.push_back(used.back());
        used.pop_back();
    }
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
        VK_CHECK(vkCreateFence(RendererBackendVk::get_dev(), &vkinfo, nullptr, &fence));
        if(name.size()) { set_debug_name(fence, name); }
    }
    else if(type == BINARY_SEMAPHORE || type == TIMELINE_SEMAPHORE)
    {
        auto vkinfo = Vks(VkSemaphoreCreateInfo{});
        const auto timeline_info =
            Vks(VkSemaphoreTypeCreateInfo{ .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = value });
        if(type == TIMELINE_SEMAPHORE) { vkinfo.pNext = &timeline_info; }
        VK_CHECK(vkCreateSemaphore(RendererBackendVk::get_dev(), &vkinfo, nullptr, &semaphore));
        if(name.size()) { set_debug_name(semaphore, name); }
    }
}

void Sync::destroy()
{
    if(type == UNKNOWN) { return; }
    if(type == FENCE) { vkDestroyFence(RendererBackendVk::get_dev(), fence, nullptr); }
    else { vkDestroySemaphore(RendererBackendVk::get_dev(), semaphore, nullptr); }
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
        vkSignalSemaphore(RendererBackendVk::get_dev(), &info);
    }
    else
    {
        ENG_ERROR("Sync object of type {} cannot be signaled on.", to_string(type));
        return;
    }
    this->value = value;
}

bool Sync::wait_cpu(size_t timeout, uint64_t value) const
{
    if(type == UNKNOWN)
    {
        ENG_ERROR("Sync object was not initialized.");
        return false;
    }
    if(value == ~0ull) { value = this->value; }
    if(type == FENCE) { return vkWaitForFences(RendererBackendVk::get_dev(), 1, &fence, true, timeout) == VK_SUCCESS; }
    else if(type == TIMELINE_SEMAPHORE)
    {
        const auto info = Vks(VkSemaphoreWaitInfo{ .semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value });
        return vkWaitSemaphores(RendererBackendVk::get_dev(), &info, timeout) == VK_SUCCESS;
    }
    ENG_ERROR("Sync object of type {} cannot be waited on.", to_string(type));
    return false;
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
    if(type == FENCE) { vkResetFences(RendererBackendVk::get_dev(), 1, &fence); }
    else
    {
        auto type = this->type;
        auto name = this->name;
        destroy();
        init({ type, value, name });
    }
}

SubmitQueue::SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept
    : dev(dev), queue(queue), family_idx(family_idx), fence(get_renderer().make_sync({ SyncType::FENCE, 0, "" }))
{
}

CommandPoolVk* SubmitQueue::make_command_pool(VkCommandPoolCreateFlags flags)
{
    return &command_pools.emplace_back(dev, family_idx, flags);
}

SubmitQueue& SubmitQueue::wait_sync(Sync* sync, Flags<PipelineStage> stages, uint64_t value)
{
    ENG_ASSERT(sync);
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
        ENG_ASSERT(!submission.fence);
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

SubmitQueue& SubmitQueue::with_cmd_buf(ICommandBuffer* cmd)
{
    submission.cmds.push_back(cmd);
    return *this;
}

void SubmitQueue::submit()
{
    for(auto& [sync, stage] : submission.pushed_syncs)
    {
        wait_sync(sync, stage);
    }

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
        cmds[i] = Vks(VkCommandBufferSubmitInfo{ .commandBuffer = ((CommandBufferVk*)submission.cmds[i])->cmd });
    }

    const auto vk_info = Vks(VkSubmitInfo2{
        .waitSemaphoreInfoCount = (uint32_t)wait_sems.size(),
        .pWaitSemaphoreInfos = wait_sems.data(),
        .commandBufferInfoCount = (uint32_t)cmds.size(),
        .pCommandBufferInfos = cmds.data(),
        .signalSemaphoreInfoCount = (uint32_t)sig_sems.size(),
        .pSignalSemaphoreInfos = sig_sems.data(),
    });
    VK_CHECK(vkQueueSubmit2(queue, 1, &vk_info, submission.fence ? submission.fence->fence : nullptr));
    submission = Submission{};
}

bool SubmitQueue::submit_wait(uint64_t timeout)
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
    ENG_ASSERT(sfence);
    submit();
    const auto wait_result = sfence->wait_cpu(timeout);
    if(is_fence_temp) { sfence->reset(); }
    return wait_result;
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
                                             .pSwapchains = &SwapchainMetadataVk::get(*swapchain).swapchain,
                                             .pImageIndices = &swapchain->current_index });
    vkQueuePresentKHR(queue, &pinfo);
    submission = {};
}

void SubmitQueue::wait_idle() { vkQueueWaitIdle(queue); }

} // namespace gfx
} // namespace eng
