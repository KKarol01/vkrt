#pragma once

#include <map>
#include <string>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

struct ImageView {
    VkImageView view{};
    VkImageViewCreateInfo info{};
    VkImageLayout layout{};
    VkSampler sampler{};
};

class Image {

  public:
    Image() noexcept = default;
    Image(const std::string& name, VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& vk_info) noexcept;
    Image(const std::string& name, VkDevice dev, VkImage image, const VkImageCreateInfo& vk_info) noexcept;
    // todo: make destructors and proper move semantics

    // Creates default view with undefined layout and nullptr sampler, and deduced parameters on undefined vk_viewinfo.format.
    VkImageView get_view();
    VkImageView get_view(const VkImageViewCreateInfo& vk_viewinfo, VkImageLayout layout, VkSampler sampler);
    const ImageView& get_view_info(VkImageView view) const;
    VkImageAspectFlags deduce_aspect() const;

  private:
    VkImageView try_find_view(const VkImageViewCreateInfo& vk_viewinfo, VkImageLayout layout, VkSampler sampler);

  public:
    std::string name;
    VkDevice dev{};
    VmaAllocator vma{};
    VkImageCreateInfo vk_info{};
    VkImage image{};
    VmaAllocation alloc{};
    VkImageView default_view{};
    std::vector<ImageView> views;
    VkImageLayout current_layout{};
};