#include <eng/common/to_vk.hpp>
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/renderer.hpp>

// clang-format off
namespace gfx
{
VkFilter to_vk(const ImageFilter& a) 
{ 
	switch(a) 
    {
        case ImageFilter::LINEAR: { return VK_FILTER_LINEAR; }
        case ImageFilter::NEAREST: { return VK_FILTER_NEAREST; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkSamplerAddressMode to_vk(const ImageAddressing& a) 
{
	switch(a) 
    {
        case ImageAddressing::REPEAT: { return VK_SAMPLER_ADDRESS_MODE_REPEAT; }
        case ImageAddressing::CLAMP_EDGE: { return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkFormat to_vk(const ImageFormat& a) 
{ 
	switch(a) 
    {
        case ImageFormat::UNDEFINED: { return VK_FORMAT_UNDEFINED; }
        case ImageFormat::R8G8B8A8_UNORM: { return VK_FORMAT_R8G8B8A8_UNORM; }
        case ImageFormat::R8G8B8A8_SRGB: { return VK_FORMAT_R8G8B8A8_SRGB; }
        case ImageFormat::D16_UNORM: { return VK_FORMAT_D16_UNORM; }
        case ImageFormat::D24_S8_UNORM: { return VK_FORMAT_D24_UNORM_S8_UINT; }
        case ImageFormat::D32_SFLOAT: { return VK_FORMAT_D32_SFLOAT; }
        case ImageFormat::R16F: { return VK_FORMAT_R16_SFLOAT; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkFormat to_vk(const VertexFormat & a)
{
    switch(a) 
    {
        case VertexFormat::R32_SFLOAT: { return VK_FORMAT_R32_SFLOAT; }
        case VertexFormat::R32G32_SFLOAT: { return VK_FORMAT_R32G32_SFLOAT; }
        case VertexFormat::R32G32B32_SFLOAT: { return VK_FORMAT_R32G32B32_SFLOAT; }
        case VertexFormat::R32G32B32A32_SFLOAT: { return VK_FORMAT_R32G32B32A32_SFLOAT; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkImageType to_vk(const ImageType& a) 
{     
    switch(a) 
    {
        case ImageType::TYPE_1D: { return VK_IMAGE_TYPE_1D; }
        case ImageType::TYPE_2D: { return VK_IMAGE_TYPE_2D; }
        case ImageType::TYPE_3D: { return VK_IMAGE_TYPE_3D; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    } 
}

VkImageViewType to_vk(const ImageViewType& a) 
{     
    switch(a) 
    {
        case ImageViewType::TYPE_1D: { return VK_IMAGE_VIEW_TYPE_1D; }
        case ImageViewType::TYPE_2D: { return VK_IMAGE_VIEW_TYPE_2D; }
        case ImageViewType::TYPE_3D: { return VK_IMAGE_VIEW_TYPE_3D; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkCullModeFlags to_vk(const CullFace& a)
{
    switch(a) 
    {
        case CullFace::NONE: { return VK_CULL_MODE_NONE; }
        case CullFace::FRONT: { return VK_CULL_MODE_FRONT_BIT; }
        case CullFace::BACK: { return VK_CULL_MODE_BACK_BIT; }
        case CullFace::FRONT_AND_BACK: { return VK_CULL_MODE_FRONT_AND_BACK; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkCompareOp to_vk(const DepthCompare& a)
{
    switch(a) 
    {
        case DepthCompare::NEVER: { return VK_COMPARE_OP_NEVER; }
        case DepthCompare::LESS: { return VK_COMPARE_OP_LESS; }
        case DepthCompare::GREATER: { return VK_COMPARE_OP_GREATER; }
        case DepthCompare::EQUAL: { return VK_COMPARE_OP_EQUAL; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkShaderStageFlagBits to_vk(const ShaderStage& a)
{
    switch(a) 
    {
        case ShaderStage::VERTEX: { return VK_SHADER_STAGE_VERTEX_BIT; }
        case ShaderStage::PIXEL: { return VK_SHADER_STAGE_FRAGMENT_BIT; }
        case ShaderStage::COMPUTE: { return VK_SHADER_STAGE_COMPUTE_BIT; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkSamplerMipmapMode to_vk(const SamplerMipmapMode a)
{
    switch(a) 
    {
        case SamplerMipmapMode::NEAREST: { return VK_SAMPLER_MIPMAP_MODE_NEAREST; }
        case SamplerMipmapMode::LINEAR: { return VK_SAMPLER_MIPMAP_MODE_LINEAR; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkSamplerReductionMode to_vk(const SamplerReductionMode a)
{
    switch(a) 
    {
        case SamplerReductionMode::MIN: { return VK_SAMPLER_REDUCTION_MODE_MIN; }
        case SamplerReductionMode::MAX: { return VK_SAMPLER_REDUCTION_MODE_MAX; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkPolygonMode to_vk(const PolygonMode & a)
{
    switch(a) 
    {
        case PolygonMode::FILL: { return VK_POLYGON_MODE_FILL; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkStencilOp to_vk(const StencilOp & a)
{
    switch(a) 
    {
        case StencilOp::KEEP: { return VK_STENCIL_OP_KEEP; }
        case StencilOp::ZERO: { return VK_STENCIL_OP_ZERO; }
        case StencilOp::REPLACE: { return VK_STENCIL_OP_REPLACE; }
        case StencilOp::INCREMENT_AND_CLAMP: { return VK_STENCIL_OP_INCREMENT_AND_CLAMP; }
        case StencilOp::DECREMENT_AND_CLAMP: { return VK_STENCIL_OP_DECREMENT_AND_CLAMP; }
        case StencilOp::INVERT: { return VK_STENCIL_OP_INVERT; }
        case StencilOp::INCREMENT_AND_WRAP: { return VK_STENCIL_OP_INCREMENT_AND_WRAP; }
        case StencilOp::DECREMENT_AND_WRAP: { return VK_STENCIL_OP_DECREMENT_AND_WRAP; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkCompareOp to_vk(const CompareOp & a)
{
    switch(a) 
    {
        case CompareOp::NEVER: { return VK_COMPARE_OP_NEVER; }
        case CompareOp::LESS: { return VK_COMPARE_OP_LESS; }
        case CompareOp::EQUAL: { return VK_COMPARE_OP_EQUAL; }
        case CompareOp::LESS_OR_EQUAL: { return VK_COMPARE_OP_LESS_OR_EQUAL; }
        case CompareOp::GREATER: { return VK_COMPARE_OP_GREATER; }
        case CompareOp::NOT_EQUAL: { return VK_COMPARE_OP_NOT_EQUAL; }
        case CompareOp::GREATER_OR_EQUAL: { return VK_COMPARE_OP_GREATER_OR_EQUAL; }
        case CompareOp::ALWAYS: { return VK_COMPARE_OP_ALWAYS; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkBlendFactor to_vk(const BlendFactor & a)
{
    switch(a) 
    {
        case BlendFactor::ZERO: { return VK_BLEND_FACTOR_ZERO; }
        case BlendFactor::ONE: { return VK_BLEND_FACTOR_ONE; }
        case BlendFactor::SRC_COLOR: { return VK_BLEND_FACTOR_SRC_COLOR; }
        case BlendFactor::ONE_MINUS_SRC_COLOR: { return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; }
        case BlendFactor::DST_COLOR: { return VK_BLEND_FACTOR_DST_COLOR; }
        case BlendFactor::ONE_MINUS_DST_COLOR: { return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR; }
        case BlendFactor::SRC_ALPHA: { return VK_BLEND_FACTOR_SRC_ALPHA; }
        case BlendFactor::ONE_MINUS_SRC_ALPHA: { return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; }
        case BlendFactor::DST_ALPHA: { return VK_BLEND_FACTOR_DST_ALPHA; }
        case BlendFactor::ONE_MINUS_DST_ALPHA: { return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; }
        case BlendFactor::CONSTANT_COLOR: { return VK_BLEND_FACTOR_CONSTANT_COLOR; }
        case BlendFactor::ONE_MINUS_CONSTANT_COLOR: { return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR; }
        case BlendFactor::CONSTANT_ALPHA: { return VK_BLEND_FACTOR_CONSTANT_ALPHA; }
        case BlendFactor::ONE_MINUS_CONSTANT_ALPHA: { return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA; }
        case BlendFactor::SRC_ALPHA_SATURATE: { return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

VkBlendOp to_vk(const BlendOp & a)
{
    switch(a) 
    {
        case BlendOp::ADD: { return VK_BLEND_OP_ADD; }
        case BlendOp::SUBTRACT: { return VK_BLEND_OP_SUBTRACT; }
        case BlendOp::REVERSE_SUBTRACT: { return VK_BLEND_OP_REVERSE_SUBTRACT; }
        case BlendOp::MIN: { return VK_BLEND_OP_MIN; }
        case BlendOp::MAX: { return VK_BLEND_OP_MAX; }
        default: { ENG_ERROR("Unhandled case"); return {}; }
    }
}

}
// clang-format on