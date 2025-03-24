#pragma once

#include <map>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

class Image {
  public:
    Image() noexcept = default;
    Image(VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& info) noexcept;
    // todo: make destructors and proper move semantics

    static bool comp_vk_img_view_create_info(const VkImageViewCreateInfo& a, const VkImageViewCreateInfo& b);

    VkImageView get_view();
    VkImageView get_view(const VkImageViewCreateInfo& info);

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
    VkImageView default_view{};
    std::map<VkImageViewCreateInfo, VkImageView, decltype(Image::comp_vk_img_view_create_info)> views;
};