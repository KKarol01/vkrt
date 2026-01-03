#pragma once
#include <vulkan/vulkan.h>
#include <eng/common/flags.hpp>

namespace eng
{
namespace gfx
{

enum class ImageFormat;
enum class ImageType;
enum class ImageViewType;
enum class ImageFilter;
enum class ImageAddressing;
enum class ImageAspect;
enum class ImageLayout;
enum class ImageUsage;
enum class CullFace;
enum class VertexFormat;
enum class DepthCompare;
enum class Topology;
enum class PolygonMode;
enum class StencilOp;
enum class CompareOp;
enum class BlendFactor;
enum class BlendOp;
enum class SamplerReductionMode;
enum class SamplerMipmapMode;
enum class BufferUsage;
enum class PipelineStage : uint32_t;
enum class PipelineAccess : uint32_t;
enum class PipelineType;
enum class ShaderStage : uint32_t;
enum class PipelineSetFlags : uint32_t;
enum class PipelineBindingFlags : uint32_t;
enum class PipelineBindingType;
enum class DescriptorPoolFlags;

VkFilter to_vk(const ImageFilter& a);
VkSamplerAddressMode to_vk(const ImageAddressing& a);
VkFormat to_vk(const ImageFormat& a);
VkFormat to_vk(const VertexFormat& a);
VkImageType to_vk(const ImageType& a);
VkImageViewType to_vk(const ImageViewType& a);
VkImageAspectFlags to_vk(const Flags<ImageAspect>& a);
VkImageLayout to_vk(const ImageLayout& a);
VkImageUsageFlags to_vk(const Flags<ImageUsage>& a);
VkCullModeFlags to_vk(const CullFace& a);
VkCompareOp to_vk(const DepthCompare& a);
VkShaderStageFlagBits to_vk(const ShaderStage& a);
VkSamplerMipmapMode to_vk(const SamplerMipmapMode a);
VkSamplerReductionMode to_vk(const SamplerReductionMode a);
VkPrimitiveTopology to_vk(const Topology& a);
VkPolygonMode to_vk(const PolygonMode& a);
VkStencilOp to_vk(const StencilOp& a);
VkCompareOp to_vk(const CompareOp& a);
VkBlendFactor to_vk(const BlendFactor& a);
VkBlendOp to_vk(const BlendOp& a);
VkBufferUsageFlags to_vk(const Flags<BufferUsage>& a);
VkPipelineStageFlags2 to_vk(const Flags<PipelineStage>& a);
VkAccessFlags2 to_vk(const Flags<PipelineAccess>& a);
VkPipelineBindPoint to_vk(const PipelineType& a);
VkShaderStageFlags to_vk(const Flags<ShaderStage>& a);
VkDescriptorSetLayoutCreateFlags to_vk(const Flags<PipelineSetFlags>& a);
VkDescriptorBindingFlags to_vk(const Flags<PipelineBindingFlags>& a);
VkDescriptorType to_vk(const PipelineBindingType& a);
VkDescriptorPoolCreateFlags to_vk(const Flags<DescriptorPoolFlags>& a);

} // namespace gfx
} // namespace eng
