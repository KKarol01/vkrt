#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/common/logger.hpp>

namespace gfx
{

CommandPool::CommandPool(VkDevice dev, uint32_t family_index, VkCommandPoolCreateFlags flags) noexcept : dev(dev)
{
    const auto vk_info = Vks(VkCommandPoolCreateInfo{ .flags = flags, .queueFamilyIndex = family_index });
    VK_CHECK(vkCreateCommandPool(dev, &vk_info, nullptr, &pool));
}

VkCommandBuffer CommandPool::allocate()
{
    if(free.empty())
    {
        VkCommandBuffer& cmd = used.emplace_back();
        const auto vk_info = Vks(VkCommandBufferAllocateInfo{
            .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 });
        VK_CHECK(vkAllocateCommandBuffers(dev, &vk_info, &cmd));
        return cmd;
    }
    auto cmd = used.emplace_back(free.front());
    free.pop_front();
    return cmd;
}

VkCommandBuffer CommandPool::begin()
{
    auto cmd = allocate();
    begin(cmd);
    return cmd;
}

VkCommandBuffer CommandPool::begin(VkCommandBuffer cmd)
{
    const auto vk_info = Vks(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    VK_CHECK(vkBeginCommandBuffer(cmd, &vk_info));
    return cmd;
}

void CommandPool::reset(VkCommandBuffer cmd) { vkResetCommandBuffer(cmd, {}); }

void CommandPool::end(VkCommandBuffer cmd) { vkEndCommandBuffer(cmd); }

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

SubmitQueue& SubmitQueue::with_cmd_buf(VkCommandBuffer cmd)
{
    submission.cmds.push_back(Vks(VkCommandBufferSubmitInfo{ .commandBuffer = cmd }));
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
