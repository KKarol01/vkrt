#pragma once

#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

class Image {
  public:
    constexpr Image() noexcept = default;
    Image(VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& info) noexcept;

    // void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
    //                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout dst_layout);
    // void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
    //                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout src_layout,
    //                        VkImageLayout dst_layout);
    // void deduce_aspect(VkImageUsageFlags usage);
    // void create_default_view(int dims);

    VkDevice dev{};
    VmaAllocator vma{};
    VkImageCreateInfo info{};
    VkImage image{};
    VmaAllocation alloc{};
};