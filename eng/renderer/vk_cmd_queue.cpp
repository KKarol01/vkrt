#include <eng/renderer/vk_cmd_queue.hpp>

VkCmdPool::VkCmdPool(uint32_t queue_index, VkCommandPoolCreateFlags flags) noexcept {}

VkCommandBuffer VkCmdPool::allocate(VkCommandBufferLevel level) { return VkCommandBuffer(); }

VkCommandBuffer VkCmdPool::begin(VkCommandBufferUsageFlags flags, VkCommandBufferLevel level) {
    return VkCommandBuffer();
}

VkCommandBuffer VkCmdPool::begin_onetime(VkCommandBufferLevel level) { return VkCommandBuffer(); }

void VkCmdPool::end(VkCommandBuffer buffer) {}

void VkCmdPool::reset() {}
