#include <eng/common/to_vk.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan_structs.hpp>

namespace eng
{
// clang-format off
VkFilter to_vk(const gfx::ImageFilter& a) 
{ 
	switch(a) 
    {
        case gfx::ImageFilter::LINEAR: { return VK_FILTER_LINEAR; }
        case gfx::ImageFilter::NEAREST: { return VK_FILTER_NEAREST; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkSamplerAddressMode to_vk(const gfx::ImageAddressing& a) 
{
	switch(a) 
    {
        case gfx::ImageAddressing::REPEAT: { return VK_SAMPLER_ADDRESS_MODE_REPEAT; }
        case gfx::ImageAddressing::CLAMP_EDGE: { return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkFormat to_vk(const gfx::ImageFormat& a) 
{ 
	switch(a) 
    {
        case gfx::ImageFormat::UNDEFINED: { return VK_FORMAT_UNDEFINED; }
        case gfx::ImageFormat::R8G8B8A8_UNORM: { return VK_FORMAT_R8G8B8_UNORM; }
        case gfx::ImageFormat::R8G8B8A8_SRGB: { return VK_FORMAT_R8G8B8A8_SRGB; }
        case gfx::ImageFormat::D16_UNORM: { return VK_FORMAT_D16_UNORM; }
        case gfx::ImageFormat::D24_S8_UNORM: { return VK_FORMAT_D24_UNORM_S8_UINT; }
        case gfx::ImageFormat::D32_SFLOAT: { return VK_FORMAT_D32_SFLOAT; }
        case gfx::ImageFormat::R16F: { return VK_FORMAT_R16_SFLOAT; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkImageType to_vk(const gfx::ImageType& a) 
{     
    switch(a) 
    {
        case gfx::ImageType::TYPE_1D: { return VK_IMAGE_TYPE_1D; }
        case gfx::ImageType::TYPE_2D: { return VK_IMAGE_TYPE_2D; }
        case gfx::ImageType::TYPE_3D: { return VK_IMAGE_TYPE_3D; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    } 
}

VkImageViewType to_vk(const gfx::ImageViewType& a) 
{     
    switch(a) 
    {
        case gfx::ImageViewType::TYPE_1D: { return VK_IMAGE_VIEW_TYPE_1D; }
        case gfx::ImageViewType::TYPE_2D: { return VK_IMAGE_VIEW_TYPE_2D; }
        case gfx::ImageViewType::TYPE_3D: { return VK_IMAGE_VIEW_TYPE_3D; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkCullModeFlags to_vk(const gfx::PipelineCreateInfo::CullMode& a)
{
    switch(a) 
    {
        case gfx::PipelineCreateInfo::CullMode::NONE: { return VK_CULL_MODE_NONE; }
        case gfx::PipelineCreateInfo::CullMode::FRONT: { return VK_CULL_MODE_FRONT_BIT; }
        case gfx::PipelineCreateInfo::CullMode::BACK: { return VK_CULL_MODE_BACK_BIT; }
        case gfx::PipelineCreateInfo::CullMode::FRONT_AND_BACK: { return VK_CULL_MODE_FRONT_AND_BACK; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkCompareOp to_vk(const gfx::PipelineCreateInfo::DepthCompare& a)
{
    switch(a) 
    {
        case gfx::PipelineCreateInfo::DepthCompare::NEVER: { return VK_COMPARE_OP_NEVER; }
        case gfx::PipelineCreateInfo::DepthCompare::LESS: { return VK_COMPARE_OP_LESS; }
        case gfx::PipelineCreateInfo::DepthCompare::GREATER: { return VK_COMPARE_OP_GREATER; }
        case gfx::PipelineCreateInfo::DepthCompare::EQUAL: { return VK_COMPARE_OP_EQUAL; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkShaderStageFlagBits to_vk(const gfx::Shader::Stage& a)
{
    switch(a) 
    {
        case gfx::Shader::Stage::VERTEX: { return VK_SHADER_STAGE_VERTEX_BIT; }
        case gfx::Shader::Stage::PIXEL: { return VK_SHADER_STAGE_FRAGMENT_BIT; }
        case gfx::Shader::Stage::COMPUTE: { return VK_SHADER_STAGE_COMPUTE_BIT; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkSamplerMipmapMode to_vk(const gfx::SamplerDescriptor::MipMapMode a)
{
    switch(a) 
    {
        case gfx::SamplerDescriptor::MipMapMode::NEAREST: { return VK_SAMPLER_MIPMAP_MODE_NEAREST; }
        case gfx::SamplerDescriptor::MipMapMode::LINEAR: { return VK_SAMPLER_MIPMAP_MODE_LINEAR; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkSamplerReductionMode to_vk(const gfx::SamplerDescriptor::ReductionMode a)
{
    switch(a) 
    {
        case gfx::SamplerDescriptor::ReductionMode::MIN: { return VK_SAMPLER_REDUCTION_MODE_MIN; }
        case gfx::SamplerDescriptor::ReductionMode::MAX: { return VK_SAMPLER_REDUCTION_MODE_MAX; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

// clang-format on

} // namespace eng
