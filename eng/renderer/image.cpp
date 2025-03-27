#include <eng/renderer/image.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

Image::Image(const std::string& name, VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& vk_info) noexcept
    : name(name), dev(dev), vma(vma), vk_info(Vks(VkImageCreateInfo{ vk_info })) {
    if(!dev || !vma) { return; }
    VmaAllocationCreateInfo vma_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    VK_CHECK(vmaCreateImage(vma, &vk_info, &vma_info, &image, &alloc, nullptr));
    if(image) { set_debug_name(image, name); }
}

VkImageView Image::get_view() {
    if(default_view) { return default_view; }
    const auto view_info = Vks(VkImageViewCreateInfo{
        .image = image,
        .viewType = vk_info.imageType == VK_IMAGE_TYPE_3D   ? VK_IMAGE_VIEW_TYPE_3D
                    : vk_info.imageType == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D
                                                            : VK_IMAGE_VIEW_TYPE_1D,
        .format = vk_info.format,
        .subresourceRange =
            VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
    });
    return get_view(view_info);
}

VkImageView Image::get_view(const VkImageViewCreateInfo& vk_info) {
    VkImageView view{};
    VK_CHECK(vkCreateImageView(dev, &vk_info, nullptr, &view));
    if(!view) { return nullptr; }
    set_debug_name(view, std::format("{}_view", name));
    return views.emplace_back(view);
}
