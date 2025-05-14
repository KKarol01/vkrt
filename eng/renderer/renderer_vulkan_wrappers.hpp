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
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/renderer.hpp>

namespace gfx {

class SamplerStorage {
  public:
    VkSampler get_sampler();
    VkSampler get_sampler(VkFilter filter, VkSamplerAddressMode address);
    VkSampler get_sampler(ImageFiltering filter, ImageAddressing address);
    VkSampler get_sampler(VkSamplerCreateInfo vk_info);

  private:
    std::vector<std::pair<VkSamplerCreateInfo, VkSampler>> samplers;
};

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

} // namespace gfx