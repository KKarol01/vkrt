#pragma once

#include <map>
#include <string>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

class Image {
  public:
    Image() noexcept = default;
    Image(const std::string& name, VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& vk_info) noexcept;
    // todo: make destructors and proper move semantics

    static bool comp_vk_img_view_create_info(const VkImageViewCreateInfo& a, const VkImageViewCreateInfo& b);

    VkImageView get_view();
    VkImageView get_view(const VkImageViewCreateInfo& vk_info);

    std::string name;
    VkDevice dev{};
    VmaAllocator vma{};
    VkImageCreateInfo vk_info{};
    VkImage image{};
    VmaAllocation alloc{};
    VkImageView default_view{};
    std::map<VkImageViewCreateInfo, VkImageView, decltype(Image::comp_vk_img_view_create_info)> views;
};