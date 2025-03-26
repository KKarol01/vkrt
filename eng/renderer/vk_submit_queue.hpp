#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

class VkCmdPool {
  public:
    VkCmdPool() noexcept = default;
    VkCmdPool(VkDevice dev, uint32_t family_index) noexcept;

    VkCommandBuffer allocate();
    VkCommandBuffer begin();
    void end(VkCommandBuffer cmd);
    void reset();

    VkDevice dev{};
    std::deque<VkCommandBuffer> free;
    std::deque<VkCommandBuffer> used;
    VkCommandPool pool{};
};

class VkSubmitQueue {
    struct Submission {
        VkFence fence{};
        std::vector<VkCommandBufferSubmitInfo> cmds;
        std::vector<VkSemaphoreSubmitInfo> wait_sems;
        std::vector<VkSemaphoreSubmitInfo> sig_sems;
    };

  public:
    VkSubmitQueue() noexcept = default;
    VkSubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept;

    VkFence make_fence(bool signaled);
    VkSemaphore make_semaphore();

    VkSubmitQueue& with_fence(VkFence fence);
    VkSubmitQueue& with_wait_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    VkSubmitQueue& with_sig_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    VkSubmitQueue& with_cmd_buf(VkCommandBuffer cmd);
    void submit();

    VkDevice dev{};
    VkQueue queue{};
    uint32_t family_idx{ ~0u };
    std::deque<VkCmdPool> command_pools;
    std::deque<VkSemaphore> semaphores;
    std::deque<VkFence> fences;
    Submission submission;
};