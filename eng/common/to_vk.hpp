#pragma once
#include <vulkan/vulkan.h>
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/renderer.hpp>

namespace gfx
{

enum class ImageFormat;
enum class ImageType;
enum class ImageViewType;
enum class ImageFilter;
enum class ImageAddressing;
enum class ShaderStage;
enum class CullFace;
enum class VertexFormat;
enum class DepthCompare;
enum class PolygonMode;
enum class StencilOp;
enum class CompareOp;
enum class BlendFactor;
enum class BlendOp;
enum class SamplerReductionMode;
enum class SamplerMipmapMode;

VkFilter to_vk(const ImageFilter& a);
VkSamplerAddressMode to_vk(const ImageAddressing& a);
VkFormat to_vk(const ImageFormat& a);
VkFormat to_vk(const VertexFormat& a);
VkImageType to_vk(const ImageType& a);
VkImageViewType to_vk(const ImageViewType& a);
VkCullModeFlags to_vk(const CullFace& a);
VkCompareOp to_vk(const DepthCompare& a);
VkShaderStageFlagBits to_vk(const ShaderStage& a);
VkSamplerMipmapMode to_vk(const SamplerMipmapMode a);
VkSamplerReductionMode to_vk(const SamplerReductionMode a);
VkPolygonMode to_vk(const PolygonMode& a);
VkStencilOp to_vk(const StencilOp& a);
VkCompareOp to_vk(const CompareOp& a);
VkBlendFactor to_vk(const BlendFactor& a);
VkBlendOp to_vk(const BlendOp& a);
} // namespace gfx