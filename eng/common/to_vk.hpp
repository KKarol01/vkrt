#pragma once
#include <vulkan/vulkan.h>
#include <eng/renderer/pipeline.hpp>

namespace gfx
{
enum class ImageFormat;
enum class ImageType;
enum class ImageViewType;
enum class CullMode;
struct ImageViewDescriptor;
} // namespace gfx

namespace eng
{
VkFormat to_vk(const gfx::ImageFormat& a);
VkImageType to_vk(const gfx::ImageType& a);
VkImageViewType to_vk(const gfx::ImageViewType& a);
VkCullModeFlags to_vk(const gfx::PipelineCreateInfo::CullMode& a);
VkCompareOp to_vk(const gfx::PipelineCreateInfo::DepthCompare& a);
VkShaderStageFlagBits to_vk(const gfx::Shader::Stage& a);

} // namespace eng