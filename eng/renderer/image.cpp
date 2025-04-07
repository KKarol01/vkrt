#include <eng/renderer/image.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

static bool compare_vkviewinfo(const VkImageViewCreateInfo& a, const VkImageViewCreateInfo& b) {
    return a.flags == b.flags && a.image == b.image && a.viewType == b.viewType && a.format == b.format &&
           (a.components.r == b.components.r && a.components.g == b.components.g && a.components.b == b.components.b &&
            a.components.a == b.components.a) &&
           (a.subresourceRange.aspectMask == b.subresourceRange.aspectMask &&
            a.subresourceRange.baseMipLevel == b.subresourceRange.baseMipLevel &&
            a.subresourceRange.levelCount == b.subresourceRange.levelCount &&
            a.subresourceRange.baseArrayLayer == b.subresourceRange.baseArrayLayer &&
            a.subresourceRange.layerCount == b.subresourceRange.layerCount);
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

VkImageView Image::get_view() { return get_view(Vks(VkImageViewCreateInfo{}), VK_IMAGE_LAYOUT_UNDEFINED, nullptr); }

VkImageView Image::get_view(const VkImageViewCreateInfo& vk_viewinfo, VkImageLayout layout, VkSampler sampler) {
    VkImageView view{};
    auto info = vk_viewinfo;
    info.image = image;
    if(info.format == VK_FORMAT_UNDEFINED) {
        info.viewType = vk_info.imageType == VK_IMAGE_TYPE_3D   ? VK_IMAGE_VIEW_TYPE_3D
                        : vk_info.imageType == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D
                                                                : VK_IMAGE_VIEW_TYPE_1D,
        info.format = vk_info.format;
        info.subresourceRange = { .aspectMask = deduce_aspect() & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT),
                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                  .layerCount = VK_REMAINING_ARRAY_LAYERS };
    }
    if(auto view = try_find_view(info, layout, sampler)) { return view; }
    VK_CHECK(vkCreateImageView(dev, &info, nullptr, &view));
    if(!view) { return nullptr; }
    set_debug_name(view, std::format("{}_view", name));
    return views.emplace_back(view, info, layout, sampler).view;
}

const ImageView& Image::get_view_info(VkImageView view) const {
    return *std::find_if(views.begin(), views.end(), [view](const auto& e) { return e.view == view; });
}

VkImageAspectFlags Image::deduce_aspect() const {
    if(vk_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkImageView Image::try_find_view(const VkImageViewCreateInfo& vk_viewinfo, VkImageLayout layout, VkSampler sampler) {
    for(const auto& e : views) {
        if(compare_vkviewinfo(vk_viewinfo, e.info) && e.layout == layout && e.sampler == sampler) { return e.view; }
    }
    return nullptr;
}