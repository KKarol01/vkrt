#include <eng/common/to_vk.hpp>
#include <eng/renderer/renderer.hpp>

// clang-format off
namespace eng
{
namespace gfx
{

VkFilter to_vk(const ImageFilter& a) 
{ 
	switch(a) 
    {
        case ImageFilter::LINEAR: { return VK_FILTER_LINEAR; }
        case ImageFilter::NEAREST: { return VK_FILTER_NEAREST; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkSamplerAddressMode to_vk(const ImageAddressing& a) 
{
	switch(a) 
    {
        case ImageAddressing::REPEAT: { return VK_SAMPLER_ADDRESS_MODE_REPEAT; }
        case ImageAddressing::CLAMP_EDGE: { return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        case ImageFormat::R32FG32FB32FA32F: { return VK_FORMAT_R32G32B32A32_SFLOAT; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkImageType to_vk(const ImageType& a) 
{     
    switch(a) 
    {
        case ImageType::TYPE_1D: { return VK_IMAGE_TYPE_1D; }
        case ImageType::TYPE_2D: { return VK_IMAGE_TYPE_2D; }
        case ImageType::TYPE_3D: { return VK_IMAGE_TYPE_3D; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    } 
}

VkImageViewType to_vk(const ImageViewType& a) 
{     
    switch(a) 
    {
        case ImageViewType::TYPE_1D: { return VK_IMAGE_VIEW_TYPE_1D; }
        case ImageViewType::TYPE_2D: { return VK_IMAGE_VIEW_TYPE_2D; }
        case ImageViewType::TYPE_3D: { return VK_IMAGE_VIEW_TYPE_3D; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkImageAspectFlags to_vk(const ImageAspect& a)
{
    switch(a)
    {
        case gfx::ImageAspect::NONE:            { return VK_IMAGE_ASPECT_NONE; }
        case gfx::ImageAspect::COLOR:           { return VK_IMAGE_ASPECT_COLOR_BIT; }
        case gfx::ImageAspect::DEPTH:           { return VK_IMAGE_ASPECT_DEPTH_BIT; }
        case gfx::ImageAspect::STENCIL:         { return VK_IMAGE_ASPECT_STENCIL_BIT; }
        case gfx::ImageAspect::DEPTH_STENCIL:   { return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkImageLayout to_vk(const ImageLayout& a)
{
    switch(a)
    {
        case gfx::ImageLayout::UNDEFINED:       { return VK_IMAGE_LAYOUT_UNDEFINED; }
        case gfx::ImageLayout::GENERAL:         { return VK_IMAGE_LAYOUT_GENERAL; }
        case gfx::ImageLayout::READ_ONLY:       { return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL; }
        case gfx::ImageLayout::ATTACHMENT:      { return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL; }
        case gfx::ImageLayout::TRANSFER_SRC:    { return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; }
        case gfx::ImageLayout::TRANSFER_DST:    { return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; }
        case gfx::ImageLayout::PRESENT:         { return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkImageUsageFlags to_vk(const Flags<ImageUsage>& a)
{
    VkImageUsageFlags flags{};
    if(a.test(gfx::ImageUsage::STORAGE_BIT))                    { flags |= VK_IMAGE_USAGE_STORAGE_BIT; }
    if(a.test(gfx::ImageUsage::SAMPLED_BIT))                    { flags |= VK_IMAGE_USAGE_SAMPLED_BIT; }
    if(a.test(gfx::ImageUsage::TRANSFER_SRC_BIT))               { flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; }
    if(a.test(gfx::ImageUsage::TRANSFER_DST_BIT))               { flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; }
    if(a.test(gfx::ImageUsage::COLOR_ATTACHMENT_BIT))           { flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; }
    if(a.test(gfx::ImageUsage::DEPTH_STENCIL_ATTACHMENT_BIT))   { flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; }
    return flags;
}

VkCullModeFlags to_vk(const CullFace& a)
{
    switch(a) 
    {
        case CullFace::NONE: { return VK_CULL_MODE_NONE; }
        case CullFace::FRONT: { return VK_CULL_MODE_FRONT_BIT; }
        case CullFace::BACK: { return VK_CULL_MODE_BACK_BIT; }
        case CullFace::FRONT_AND_BACK: { return VK_CULL_MODE_FRONT_AND_BACK; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkShaderStageFlagBits to_vk(const ShaderStage& a)
{
    switch(a) 
    {
        case ShaderStage::VERTEX_BIT:           { return VK_SHADER_STAGE_VERTEX_BIT; }
        case ShaderStage::PIXEL_BIT:            { return VK_SHADER_STAGE_FRAGMENT_BIT; }
        case ShaderStage::COMPUTE_BIT:          { return VK_SHADER_STAGE_COMPUTE_BIT; }
        case ShaderStage::RAYGEN_BIT:           { return VK_SHADER_STAGE_RAYGEN_BIT_KHR; }
        case ShaderStage::ANY_HIT_BIT:          { return VK_SHADER_STAGE_ANY_HIT_BIT_KHR; }
        case ShaderStage::CLOSEST_HIT_BIT:      { return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; }
        case ShaderStage::MISS_BIT:             { return VK_SHADER_STAGE_MISS_BIT_KHR; }
        case ShaderStage::INTERSECTION_BIT:     { return VK_SHADER_STAGE_INTERSECTION_BIT_KHR; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkSamplerMipmapMode to_vk(const SamplerMipmapMode a)
{
    switch(a) 
    {
        case SamplerMipmapMode::NEAREST: { return VK_SAMPLER_MIPMAP_MODE_NEAREST; }
        case SamplerMipmapMode::LINEAR: { return VK_SAMPLER_MIPMAP_MODE_LINEAR; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkSamplerReductionMode to_vk(const SamplerReductionMode a)
{
    switch(a) 
    {
        case SamplerReductionMode::MIN: { return VK_SAMPLER_REDUCTION_MODE_MIN; }
        case SamplerReductionMode::MAX: { return VK_SAMPLER_REDUCTION_MODE_MAX; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkPolygonMode to_vk(const PolygonMode & a)
{
    switch(a) 
    {
        case PolygonMode::FILL: { return VK_POLYGON_MODE_FILL; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        default: { ENG_ERROR("Unhandled case."); return {}; }
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
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkBufferUsageFlags to_vk(const Flags<BufferUsage>& a)
{
    VkBufferUsageFlags flags{};
    if(a.test(gfx::BufferUsage::INDEX_BIT))         { flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; }
    if(a.test(gfx::BufferUsage::STORAGE_BIT))       { flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; }
    if(a.test(gfx::BufferUsage::INDIRECT_BIT))      { flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT; }
    if(a.test(gfx::BufferUsage::TRANSFER_SRC_BIT))  { flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT; }
    if(a.test(gfx::BufferUsage::TRANSFER_DST_BIT))  { flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT; }
    return flags;
}

VkPipelineStageFlags2 to_vk(const Flags<PipelineStage>& a)
{
    VkPipelineStageFlags2 flags{};
    if(a == gfx::PipelineStage::NONE)               { flags |= VK_PIPELINE_STAGE_2_NONE; }
    if(a == gfx::PipelineStage::ALL)                { flags |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; }
    if(a.test(gfx::PipelineStage::TRANSFER_BIT))    { flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT; }
    if(a.test(gfx::PipelineStage::EARLY_Z_BIT))     { flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT; }
    if(a.test(gfx::PipelineStage::LATE_Z_BIT))      { flags |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT; }
    if(a.test(gfx::PipelineStage::COLOR_OUT_BIT))   { flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; }
    if(a.test(gfx::PipelineStage::COMPUTE_BIT))     { flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; }
    if(a.test(gfx::PipelineStage::INDIRECT_BIT))    { flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT; }
    return flags;
}

VkAccessFlags2 to_vk(const Flags<PipelineAccess>& a)
{
    VkAccessFlags2 flags{};
    if(a == gfx::PipelineAccess::NONE)                  { flags |= VK_ACCESS_2_NONE; }
    if(a.test(gfx::PipelineAccess::SHADER_READ_BIT))    { flags |= VK_ACCESS_2_SHADER_READ_BIT; }
    if(a.test(gfx::PipelineAccess::SHADER_WRITE_BIT))   { flags |= VK_ACCESS_2_SHADER_WRITE_BIT; }
    if(a.test(gfx::PipelineAccess::COLOR_READ_BIT))     { flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT; }
    if(a.test(gfx::PipelineAccess::COLOR_WRITE_BIT))    { flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; }
    if(a.test(gfx::PipelineAccess::DS_READ_BIT))        { flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT; }
    if(a.test(gfx::PipelineAccess::DS_WRITE_BIT))       { flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; }
    if(a.test(gfx::PipelineAccess::STORAGE_READ_BIT))   { flags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT; }
    if(a.test(gfx::PipelineAccess::STORAGE_WRITE_BIT))  { flags |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT; }
    if(a.test(gfx::PipelineAccess::INDIRECT_READ_BIT))  { flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT; }
    if(a.test(gfx::PipelineAccess::TRANSFER_READ_BIT))  { flags |= VK_ACCESS_2_TRANSFER_READ_BIT; }
    if(a.test(gfx::PipelineAccess::TRANSFER_WRITE_BIT)) { flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT; }
    return flags;
}

VkPipelineBindPoint to_vk(const PipelineType & a)
{
    switch(a) 
    {
        case PipelineType::GRAPHICS:    { return VK_PIPELINE_BIND_POINT_GRAPHICS; }
        case PipelineType::COMPUTE:     { return VK_PIPELINE_BIND_POINT_COMPUTE; }
        case PipelineType::RAYTRACING:  { return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR; }
        default: { ENG_ERROR("Unhandled case."); return {}; }
    }
}

VkShaderStageFlags to_vk(const Flags<ShaderStage>& a)
{
    VkShaderStageFlags flags{};
    if(a.test(ShaderStage::ALL))                { flags |= VK_SHADER_STAGE_ALL; }
    if(a.test(ShaderStage::VERTEX_BIT))         { flags |= VK_SHADER_STAGE_VERTEX_BIT; }
    if(a.test(ShaderStage::PIXEL_BIT))          { flags |= VK_SHADER_STAGE_FRAGMENT_BIT; }
    if(a.test(ShaderStage::COMPUTE_BIT))        { flags |= VK_SHADER_STAGE_COMPUTE_BIT; }
    if(a.test(ShaderStage::RAYGEN_BIT))         { flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR; }
    if(a.test(ShaderStage::ANY_HIT_BIT))        { flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR; }
    if(a.test(ShaderStage::CLOSEST_HIT_BIT))    { flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; }
    if(a.test(ShaderStage::MISS_BIT))           { flags |= VK_SHADER_STAGE_MISS_BIT_KHR; }
    if(a.test(ShaderStage::INTERSECTION_BIT))   { flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR; }
    return flags;
}

}
}
// clang-format on