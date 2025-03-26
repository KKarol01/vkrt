#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

struct QueueCmdSubmission : public VkCommandBufferSubmitInfo {
    QueueCmdSubmission(VkCommandBuffer cmd)
        : VkCommandBufferSubmitInfo(Vks(VkCommandBufferSubmitInfo{ .commandBuffer = cmd })) {}
};

struct QueueSemaphoreSubmission : public VkSemaphoreSubmitInfo {
    QueueSemaphoreSubmission(VkPipelineStageFlags2 stage, Semaphore& sem, uint32_t value = 0)
        : VkSemaphoreSubmitInfo(Vks(VkSemaphoreSubmitInfo{ .semaphore = sem.semaphore, .value = value, .stageMask = stage })) {}
};

struct QueueSubmission {
    std::vector<QueueCmdSubmission> cmds{};
    std::vector<QueueSemaphoreSubmission> wait_sems{};
    std::vector<QueueSemaphoreSubmission> signal_sems{};
};

class VkCmdPool {
  public:
    VkCmdPool() noexcept = default;
    VkCmdPool(uint32_t queue_index, VkCommandPoolCreateFlags flags = {}) noexcept;

    VkCommandBuffer allocate(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin(VkCommandBufferUsageFlags flags = {}, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin_onetime(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    void end(VkCommandBuffer buffer);
    void reset();

    std::deque<VkCommandBuffer> free;
    std::deque<VkCommandBuffer> used;
    VkCommandPool cmdpool{};
};

class VkCmdQueue {
  public:
    VkQueue queue{};
    uint32_t idx{ ~0u };
    std::vector<VkCommandPool> command_pools;
};