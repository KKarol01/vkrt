#pragma once

#include <array>
#include <filesystem>
#include <vulkan/vulkan.h>
#include <eng/common/hash.hpp>
#include <eng/renderer/resources/resources.hpp>

namespace gfx
{

struct Shader
{
    enum class Stage
    {
        NONE,
        VERTEX,
        PIXEL,
        COMPUTE
    };

    auto operator==(const Shader& o) const { return path == o.path; }

    std::filesystem::path path;
    Stage stage{ Stage::NONE };
    void* metadata{};
};

struct PipelineCreateInfo
{
    enum class CullMode
    {
        NONE,
        FRONT,
        BACK,
        FRONT_AND_BACK,
    };

    struct VertexBinding
    {
        auto operator<=>(const VertexBinding&) const = default;
        uint32_t binding;
        uint32_t stride;
        bool instanced{ false };
    };

    enum class VertexFormat
    {
        FLOAT,
        UINT,
    };

    struct VertexAttribute
    {
        auto operator<=>(const VertexAttribute&) const = default;
        uint32_t location;
        uint32_t binding;
        VertexFormat format_type{ VertexFormat::FLOAT };
        uint32_t format_count;
        uint32_t offset;
    };

    enum class DepthCompare
    {
        NEVER,
        LESS,
        GREATER,
        EQUAL
    };

    auto operator<=>(const PipelineCreateInfo&) const = default;

    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;
    std::vector<ImageFormat> color_formats{ ImageFormat::R8G8B8A8_SRGB };
    ImageFormat depth_format{ ImageFormat::UNDEFINED };
    CullMode culling{ CullMode::NONE };
    std::vector<Handle<Shader>> shaders;
    bool depth_test{ false };
    bool depth_write{ false };
    DepthCompare depth_compare{ DepthCompare::NEVER };
};

struct Pipeline
{
    auto operator==(const Pipeline& o) const { return (info <=> o.info) == 0; }
    PipelineCreateInfo info;
    void* metadata{};
};

} // namespace gfx

DEFINE_STD_HASH(gfx::PipelineCreateInfo::VertexBinding, eng::hash::combine_fnv1a(t.binding, t.stride, t.instanced));
DEFINE_STD_HASH(gfx::PipelineCreateInfo::VertexAttribute,
                eng::hash::combine_fnv1a(t.location, t.binding, t.format_type, t.format_count, t.offset));
DEFINE_STD_HASH(gfx::PipelineCreateInfo, [&t] {
    uint64_t hash = 0;
    // clang-format off
    for(const auto& e : t.bindings) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.attributes) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.color_formats) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.shaders) { hash = eng::hash::combine_fnv1a(hash, e); }
    // clang-format on
    hash = eng::hash::combine_fnv1a(hash, t.culling, t.depth_test, t.depth_write, t.depth_compare, t.depth_format);
    return hash;
}());
DEFINE_STD_HASH(gfx::Pipeline, eng::hash::combine_fnv1a(t.info));
DEFINE_STD_HASH(gfx::Shader, eng::hash::combine_fnv1a(t.path));