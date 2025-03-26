#include <eng/renderer/vk_cmd_queue.hpp>
#include <eng/renderer/vulkan_structs.hpp>

VkCmdPool::VkCmdPool(VkDevice dev, uint32_t family_index) noexcept : dev(dev) {
    const auto info =
        Vks(VkCommandPoolCreateInfo{ .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = family_index });
    VK_CHECK(vkCreateCommandPool(dev, &info, nullptr, &pool));
}

VkCommandBuffer VkCmdPool::allocate() {
    if(free.empty()) {
        VkCommandBuffer& cmd = used.emplace_back();
        const auto info = Vks(VkCommandBufferAllocateInfo{
            .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 });
        VK_CHECK(vkAllocateCommandBuffers(dev, &info, &cmd));
        return cmd;
    }
    auto cmd = used.emplace_back(free.front());
    free.pop_front();
    return cmd;
}

VkCommandBuffer VkCmdPool::begin() {
    const auto info = Vks(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    auto cmd = allocate();
    VK_CHECK(vkBeginCommandBuffer(cmd, &info));
    return cmd;
}

void VkCmdPool::end(VkCommandBuffer cmd) { vkEndCommandBuffer(cmd); }

void VkCmdPool::reset() { VK_CHECK(vkResetCommandPool(dev, pool, VkCommandPoolResetFlags{})); }

VkSubmitQueue::VkSubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept
    : dev(dev), queue(queue), family_idx(family_idx) {}

VkFence VkSubmitQueue::make_fence(bool signaled) {
    const auto info = Vks(VkFenceCreateInfo{ .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{} });
    auto& fence = fences.emplace_back();
    VK_CHECK(vkCreateFence(dev, &info, nullptr, &fence));
    return fence;
}

VkSemaphore VkSubmitQueue::make_semaphore() {
    const auto info = Vks(VkSemaphoreCreateInfo{});
    auto& sem = semaphores.emplace_back();
    VK_CHECK(vkCreateSemaphore(dev, &info, nullptr, &sem));
    return sem;
}

VkSubmitQueue& VkSubmitQueue::with_fence(VkFence fence) {
    if(submission.fence) { ENG_WARN("Overwriting already defined fence in submission"); }
    submission.fence = fence;
    return *this;
}

VkSubmitQueue& VkSubmitQueue::with_wait_sem(VkSemaphore sem, VkPipelineStageFlags2 stages) {
    submission.wait_sems.push_back(Vks(VkSemaphoreSubmitInfo{ .semaphore = sem, .stageMask = stages }));
    return *this;
}

VkSubmitQueue& VkSubmitQueue::with_sig_sem(VkSemaphore sem, VkPipelineStageFlags2 stages) {
    submission.wait_sems.push_back(Vks(VkSemaphoreSubmitInfo{ .semaphore = sem, .stageMask = stages }));
    return *this;
}

VkSubmitQueue& VkSubmitQueue::with_cmd_buf(VkCommandBuffer cmd) {
    submission.cmds.push_back(Vks(VkCommandBufferSubmitInfo{ .commandBuffer = cmd }));
    return *this;
}

void VkSubmitQueue::submit() {
    const auto info = Vks(VkSubmitInfo2{
        .waitSemaphoreInfoCount = (uint32_t)submission.wait_sems.size(),
        .pWaitSemaphoreInfos = submission.wait_sems.data(),
        .commandBufferInfoCount = (uint32_t)submission.cmds.size(),
        .pCommandBufferInfos = submission.cmds.data(),
        .signalSemaphoreInfoCount = (uint32_t)submission.sig_sems.size(),
        .pSignalSemaphoreInfos = submission.sig_sems.data(),
    });
    VK_CHECK(vkQueueSubmit2(queue, 1, &info, submission.fence));
    submission = Submission{};
}
