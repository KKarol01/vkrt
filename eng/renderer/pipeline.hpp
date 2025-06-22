#pragma once

#include <array>
#include <vulkan/vulkan.h>
#include <eng/common/hash.hpp>

namespace gfx
{

struct RasterizationSettings
{
    bool operator==(const RasterizationSettings& o) const
    {
        return std::hash<RasterizationSettings>{}(*this) == std::hash<RasterizationSettings>{}(o);
    }
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
    PipelineSettings settings{};
    VkPipelineBindPoint bind_point{};
    VkPipeline pipeline{};
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

namespace std
{
template <> struct hash<gfx::RasterizationSettings>
{
    size_t operator()(const gfx::RasterizationSettings& a) const
    {
        return HashCombine::hash(a.num_col_formats, a.col_formats[0], a.col_formats[1], a.col_formats[2],
                                 a.col_formats[3], a.dep_format, a.culling, a.depth_test, a.depth_write, a.depth_op);
    }
};
} // namespace std