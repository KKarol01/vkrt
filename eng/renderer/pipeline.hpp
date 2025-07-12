#pragma once

#include <array>
#include <filesystem>
#include <vulkan/vulkan.h>
#include <eng/common/hash.hpp>
#include <eng/renderer/resources/resources.hpp>

namespace gfx
{

struct RasterizationSettings
{
    bool operator==(const RasterizationSettings& o) const;
    uint32_t num_col_formats{ 1 };
    std::array<VkFormat, 4> col_formats{ { VK_FORMAT_R8G8B8A8_SRGB } };
    VkFormat dep_format{ VK_FORMAT_D24_UNORM_S8_UINT };
    VkCullModeFlags culling{ VK_CULL_MODE_BACK_BIT };
    bool depth_test{ false };
    bool depth_write{ true };
    VkCompareOp depth_op{ VK_COMPARE_OP_LESS };
};

struct RaytracingSettings
{
    bool operator==(const RaytracingSettings& o) const
    {
        return recursion_depth == o.recursion_depth && sbt_buffer == o.sbt_buffer && groups.size() == o.groups.size() &&
               [this, &o] {
                   for(const auto& e : groups)
                   {
                       if(std::find_if(o.groups.begin(), o.groups.end(), [&e](auto& g) {
                              return e.type == g.type && e.generalShader == g.generalShader &&
                                     e.closestHitShader == g.closestHitShader && e.anyHitShader == g.anyHitShader &&
                                     e.intersectionShader == g.intersectionShader;
                          }) == o.groups.end())
                       {
                           return false;
                       }
                   }
                   return true;
               }();
    }
    uint32_t recursion_depth{ 1 };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    Handle<Buffer> sbt_buffer;
};

struct PipelineSettings
{
    std::variant<std::monostate, RasterizationSettings> settings; // monostate for, for example, compute pipeline
    std::vector<std::filesystem::path> shaders;
};

struct Shader
{
    std::filesystem::path path;
    VkShaderStageFlagBits stage{};
    VkShaderModule shader{};
};

struct Pipeline
{
    Pipeline& setDefaults();
    Pipeline& addInputBinding(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate);
    Pipeline& addInputAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);
    Pipeline& setInputAssemblyState(VkPrimitiveTopology topology, VkBool32 primitiveRestartEnable);
    // Pipeline& setTesselationState(uint32_t patchControlPoints);
    Pipeline& addViewport(VkViewport pViewports);
    Pipeline& addScissor(VkRect2D pScissors);
    Pipeline& setRasterizationState(VkPolygonMode polygonMode, VkCullModeFlags cullMode, VkFrontFace frontFace);
    Pipeline& setDepthStencilState(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp depthCompareOp);
    Pipeline& setColorBlendState(VkBool32 logicOpEnable, VkLogicOp logicOp, float blendConstants[4]);
    Pipeline& addColorAttachment(VkBool32 blendEnable, VkBlendFactor srcColorBlendFactor, VkBlendFactor dstColorBlendFactor,
                                 VkBlendOp colorBlendOp, VkBlendFactor srcAlphaBlendFactor, VkBlendFactor dstAlphaBlendFactor,
                                 VkBlendOp alphaBlendOp, VkColorComponentFlags colorWriteMask);
    Pipeline& addColorAttachment();
    Pipeline& addDefaultColorAttachment();
    Pipeline& addDynamicState(void* state, size_t size);
    Pipeline& addShaders(const Shader* shaders, size_t count);

    VkPipelineBindPoint bind_point{};
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    VkPipelineVertexInputStateCreateInfo input_state;
    VkPipelineInputAssemblyStateCreateInfo assembly_state;
    VkPipelineTessellationStateCreateInfo tesselation_state;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineRasterizationStateCreateInfo rasterization_state;
    VkPipelineMultisampleStateCreateInfo multisample_state;
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    VkPipelineColorBlendStateCreateInfo color_blend_state;
    std::vector<VkVertexInputBindingDescription> inputs;
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::vector<VkViewport> viewports;
    std::vector<VkRect2D> scissors;
    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments;
    std::vector<void*> dynamic_states;
    std::vector<const Shader*> shaders;
};

class PipelineCompiler
{
  public:
    Shader* get_shader(const std::filesystem::path path);
    Pipeline* get_pipeline(const PipelineSettings& settings);
    void threaded_compile();
    void compile_shader(Shader* shader);
    void compile_pipeline(Pipeline* pipeline);
    VkShaderStageFlagBits get_shader_stage(std::filesystem::path path) const;
    void canonize_path(std::filesystem::path& p);

    std::forward_list<Shader> shaders;
    std::forward_list<Pipeline> pipelines;
    std::unordered_map<std::filesystem::path, Shader*> compiled_shaders;
    std::vector<Pipeline*> pipelines_to_compile;
    std::vector<Shader*> shaders_to_compile;
};

} // namespace gfx

DEFINE_STD_HASH(gfx::RasterizationSettings, [&t] {
    uint32_t hash = 0;
    for(auto i = 0; i < t.num_col_formats; ++i)
    {
        eng::hash::combine_fnv1a(hash, t.col_formats[i]);
    }
    return eng::hash::combine_fnv1a(t.num_col_formats, hash, t.dep_format, t.culling, t.depth_test, t.depth_write, t.depth_op);
}());