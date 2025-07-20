#pragma once
#include <vulkan/vulkan.h>
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/renderer.hpp>

namespace eng
{
VkFilter to_vk(const gfx::ImageFilter& a);
VkSamplerAddressMode to_vk(const gfx::ImageAddressing& a);
VkFormat to_vk(const gfx::ImageFormat& a);
VkImageType to_vk(const gfx::ImageType& a);
VkImageViewType to_vk(const gfx::ImageViewType& a);
VkCullModeFlags to_vk(const gfx::PipelineCreateInfo::CullMode& a);
VkCompareOp to_vk(const gfx::PipelineCreateInfo::DepthCompare& a);
VkShaderStageFlagBits to_vk(const gfx::Shader::Stage& a);
VkSamplerMipmapMode to_vk(const gfx::SamplerDescriptor::MipMapMode a);
VkSamplerReductionMode to_vk(const gfx::SamplerDescriptor::ReductionMode a);
} // namespace eng