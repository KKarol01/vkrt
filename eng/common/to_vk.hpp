#pragma once
#include <vulkan/vulkan.h>

namespace gfx
{
enum class ImageFormat;
enum class ImageType;
enum class ImageViewType;
struct ImageViewDescriptor;
} // namespace gfx

namespace eng
{
VkFormat to_vk(const gfx::ImageFormat& a);
VkImageType to_vk(const gfx::ImageType& a);
VkImageViewType to_vk(const gfx::ImageViewType& a);

} // namespace eng