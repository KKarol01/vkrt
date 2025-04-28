#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

namespace gfx {

class CommandPool {
  public:
    CommandPool() noexcept = default;
    CommandPool(VkDevice dev, uint32_t family_index, VkCommandPoolCreateFlags flags) noexcept;

    VkCommandBuffer allocate();
    VkCommandBuffer begin();
    VkCommandBuffer begin(VkCommandBuffer cmd);
    void end(VkCommandBuffer cmd);
    void reset();
    void reset(VkCommandBuffer cmd);

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

    CommandPool* make_command_pool(VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    VkFence make_fence(bool signaled);
    VkSemaphore make_semaphore();
    void reset_fence(VkFence fence);
    void destroy_fence(VkFence fence);
    VkResult wait_fence(VkFence fence, uint64_t timeout);

    SubmitQueue& with_fence(VkFence fence);
    SubmitQueue& with_wait_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    SubmitQueue& with_sig_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    SubmitQueue& with_cmd_buf(VkCommandBuffer cmd);
    VkResult submit();
    VkResult submit_wait(uint64_t timeout);
    void wait_idle();

    VkDevice dev{};
    VkQueue queue{};
    uint32_t family_idx{ ~0u };
    std::deque<CommandPool> command_pools;
    std::deque<VkSemaphore> semaphores;
    std::deque<VkFence> fences;
    Submission submission;
};

} // namespace gfx
