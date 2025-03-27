#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

class CommandPool {
  public:
    CommandPool() noexcept = default;
    CommandPool(VkDevice dev, uint32_t family_index) noexcept;

    VkCommandBuffer allocate();
    VkCommandBuffer begin();
    void end(VkCommandBuffer cmd);
    void reset();

    VkDevice dev{};
    std::deque<VkCommandBuffer> free;
    std::deque<VkCommandBuffer> used;
    VkCommandPool pool{};
};

class SubmitQueue {
    struct Submission {
        VkFence fence{};
        std::vector<VkCommandBufferSubmitInfo> cmds;
        std::vector<VkSemaphoreSubmitInfo> wait_sems;
        std::vector<VkSemaphoreSubmitInfo> sig_sems;
    };

  public:
    SubmitQueue() noexcept = default;
    SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept;

    VkFence make_fence(bool signaled);
    VkSemaphore make_semaphore();

    SubmitQueue& with_fence(VkFence fence);
    SubmitQueue& with_wait_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    SubmitQueue& with_sig_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    SubmitQueue& with_cmd_buf(VkCommandBuffer cmd);
    void submit();

    VkDevice dev{};
    VkQueue queue{};
    uint32_t family_idx{ ~0u };
    std::deque<CommandPool> command_pools;
    std::deque<VkSemaphore> semaphores;
    std::deque<VkFence> fences;
    Submission submission;
};