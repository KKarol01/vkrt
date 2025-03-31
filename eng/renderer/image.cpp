#include <eng/renderer/image.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

Image::Image(const std::string& name, VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& vk_info) noexcept
    : name(name), dev(dev), vma(vma), vk_info(Vks(VkImageCreateInfo{ vk_info })), current_layout(vk_info.initialLayout) {
    if(!dev || !vma) { return; }
    VmaAllocationCreateInfo vma_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    VK_CHECK(vmaCreateImage(vma, &this->vk_info, &vma_info, &image, &alloc, nullptr));
    if(image) { set_debug_name(image, name); }
}

Image::Image(const std::string& name, VkDevice dev, VkImage image, const VkImageCreateInfo& vk_info) noexcept
    : name(name), dev(dev), image(image), vk_info(Vks(VkImageCreateInfo{ vk_info })) {
    if(image) { set_debug_name(image, name); }
}

VkImageView Image::get_view() {
    if(default_view) { return default_view; }
    default_view = get_view(Vks(VkImageViewCreateInfo{}));
    return default_view;
}

VkImageView Image::get_view(const VkImageViewCreateInfo& vk_info) {
    VkImageView view{};
    auto info = vk_info;
    info.image = image;
    if(info.format == VK_FORMAT_UNDEFINED) {
        info.viewType = this->vk_info.imageType == VK_IMAGE_TYPE_3D   ? VK_IMAGE_VIEW_TYPE_3D
                        : this->vk_info.imageType == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D
                                                                      : VK_IMAGE_VIEW_TYPE_1D,
        info.format = this->vk_info.format;
        info.subresourceRange = { .aspectMask = deduce_aspect() & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT),
                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                  .layerCount = VK_REMAINING_ARRAY_LAYERS };
    }
    VK_CHECK(vkCreateImageView(dev, &info, nullptr, &view));
    if(!view) { return nullptr; }
    set_debug_name(view, std::format("{}_view", name));
    return views.emplace_back(view);
}

VkImageAspectFlags Image::deduce_aspect() const {
    if(vk_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}
