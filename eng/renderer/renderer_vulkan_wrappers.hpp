#pragma once
#include <vector>
#include <string>
#include <deque>
#include <variant>
#include <utility>
#include <span>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include "../common/types.hpp"
#include "vulkan_structs.hpp"
#include "renderer.hpp"

class SamplerStorage {
  public:
    VkSampler get_sampler();
    VkSampler get_sampler(VkFilter filter, VkSamplerAddressMode address);
    VkSampler get_sampler(ImageFilter filter, ImageAddressing address);
    VkSampler get_sampler(VkSamplerCreateInfo info);

  private:
    std::vector<std::pair<VkSamplerCreateInfo, VkSampler>> samplers;
};

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

class CommandPool {
  public:
    CommandPool() noexcept = default;
    CommandPool(uint32_t queue_index, VkCommandPoolCreateFlags flags = {}) noexcept;
    ~CommandPool() noexcept;

    CommandPool(const CommandPool&) noexcept = delete;
    CommandPool& operator=(const CommandPool&) noexcept = delete;
    CommandPool(CommandPool&& other) noexcept;
    CommandPool& operator=(CommandPool&& other) noexcept;

    VkCommandBuffer allocate(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin(VkCommandBufferUsageFlags flags = {}, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin_onetime(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    void end(VkCommandBuffer buffer);
    void reset();

    std::deque<VkCommandBuffer> free;
    std::deque<VkCommandBuffer> used;
    VkCommandPool cmdpool{};
};
