#include <eng/renderer/image.hpp>
#include <eng/renderer/vulkan_structs.hpp>

// void Image::transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
//                               VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout dst_layout) {
//     transition_layout(cmd, src_stage, src_access, dst_stage, dst_access, current_layout, dst_layout);
// }
//
// void Image::transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
//                               VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout src_layout,
//                               VkImageLayout dst_layout) {
//     auto imgb = Vks(VkImageMemoryBarrier2{
//         .srcStageMask = src_stage,
//         .srcAccessMask = src_access,
//         .dstStageMask = dst_stage,
//         .dstAccessMask = dst_access,
//         .oldLayout = src_layout,
//         .newLayout = dst_layout,
//         .image = image,
//         .subresourceRange = {
//             .aspectMask = aspect,
//             .baseMipLevel = 0,
//             .levelCount = mips,
//             .baseArrayLayer = 0,
//             .layerCount = layers,
//         },
//     });
//     auto dep = Vks(VkDependencyInfo{});
//     dep.pImageMemoryBarriers = &imgb;
//     dep.imageMemoryBarrierCount = 1;
//     vkCmdPipelineBarrier2(cmd, &dep);
//     current_layout = dst_layout;
// }
//
// void Image::_deduce_aspect(VkImageUsageFlags usage) {
//     aspect = VK_IMAGE_ASPECT_COLOR_BIT;
//     if(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
//         if(format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM) {
//             aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
//         } else if(format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
//             aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
//         } else if(format == VK_FORMAT_S8_UINT) {
//             aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
//         } else {
//             ENG_WARN("Unrecognized format for view aspect");
//         }
//     }
// }
//
// void Image::_create_default_view(int dims) {
//     --dims;
//     VkImageViewType view_types[]{ VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D };
//     const auto aspect = (this->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : this->aspect;
//     const auto ivinfo = Vks(VkImageViewCreateInfo{
//         .image = image,
//         .viewType = view_types[dims],
//         .format = format,
//         .components = {},
//         .subresourceRange = { .aspectMask = aspect, .baseMipLevel = 0, .levelCount = mips, .baseArrayLayer = 0, .layerCount = 1 },
//     });
//
//     VK_CHECK(vkCreateImageView(get_renderer().dev, &ivinfo, nullptr, &view));
//  }

Image::Image(VkDevice dev, VmaAllocator vma, const VkImageCreateInfo& info) noexcept : dev(dev), vma(vma), info(info) {
    if(!dev || !vma) { return; }
    
    this->info = Vks(std::move(this->info));

    // todo: handle info.pNext
    VK_CHECK(vkCreateImage(dev, &info, nullptr, &image));
}
