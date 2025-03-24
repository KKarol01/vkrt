#include <eng/renderer/image.hpp>
#include <eng/renderer/vulkan_structs.hpp>

Image::Image(VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& info) noexcept : dev(dev), vma(vma), info(info) {
    if(!dev || !vma) { return; }

    this->info = Vks(std::move(this->info));

    // todo: handle info.pNext
    VK_CHECK(vkCreateImage(dev, &info, nullptr, &image));
}

bool Image::comp_vk_img_view_create_info(const VkImageViewCreateInfo& a, const VkImageViewCreateInfo& b) {
    // if(a.pNext != b.pNext) { return false; } // todo: compare pnext?
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

VkImageView Image::get_view() {
    if(default_view) { return default_view; }

    const auto view_info = Vks(VkImageViewCreateInfo{
        .image = image,
        .viewType = info.imageType == VK_IMAGE_TYPE_3D   ? VK_IMAGE_VIEW_TYPE_3D
                    : info.imageType == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D
                                                         : VK_IMAGE_VIEW_TYPE_1D,
        .format = info.format,
        .subresourceRange =
            VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
    });
    return get_view(view_info);
}

VkImageView Image::get_view(const VkImageViewCreateInfo& info) {
    if(auto it = views.find(info); it != views.end()) { return it->second; }
    VkImageView view{};
    VK_CHECK(vkCreateImageView(dev, &info, nullptr, &view));
    if(!view) { return nullptr; }
    return views.emplace(info, view).first->second;
}
