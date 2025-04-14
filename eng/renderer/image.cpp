#include <eng/renderer/image.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

static bool compare_view_infos(const VkImageViewCreateInfo& a, const VkImageViewCreateInfo& b) {
    // const void* pNext;
    if(a.flags != b.flags) { return false; }
    if(a.image != b.image) { return false; }
    if(a.viewType != b.viewType) { return false; }
    if(a.format != b.format) { return false; }
    if(a.components.r != b.components.r || a.components.g != b.components.g || a.components.b != b.components.b ||
       a.components.a != b.components.a) {
        return false;
    }
    if(a.subresourceRange.aspectMask != b.subresourceRange.aspectMask ||
       a.subresourceRange.baseMipLevel != b.subresourceRange.baseMipLevel ||
       a.subresourceRange.levelCount != b.subresourceRange.levelCount ||
       a.subresourceRange.baseArrayLayer != b.subresourceRange.baseArrayLayer ||
       a.subresourceRange.layerCount != b.subresourceRange.layerCount) {
        return false;
    }
    return true;
}

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

VkImageView Image::get_view(VkImageViewCreateInfo vk_info) {
    VkImageView view{};
    vk_info.image = image;
    if(vk_info.format == VK_FORMAT_UNDEFINED) {
        vk_info.viewType = this->vk_info.imageType == VK_IMAGE_TYPE_3D   ? VK_IMAGE_VIEW_TYPE_3D
                           : this->vk_info.imageType == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D
                                                                         : VK_IMAGE_VIEW_TYPE_1D,
        vk_info.format = this->vk_info.format;
        vk_info.subresourceRange = { .aspectMask = deduce_aspect() & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT),
                                     .levelCount = VK_REMAINING_MIP_LEVELS,
                                     .layerCount = VK_REMAINING_ARRAY_LAYERS };
    }

    if(auto it = std::find_if(views.begin(), views.end(),
                              [&vk_info](const auto& e) { return compare_view_infos(e.first, vk_info); });
       it != views.end()) {
        return it->second;
    }

    VK_CHECK(vkCreateImageView(dev, &vk_info, nullptr, &view));
    if(!view) { return nullptr; }
    set_debug_name(view, std::format("{}_view", name));
    return views.emplace_back(vk_info, view).second;
}

VkImageAspectFlags Image::deduce_aspect() const {
    if(vk_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}
