#pragma once

#include <filesystem>
#include <vulkan/vulkan.h>
#include <eng/renderer/renderer.hpp>
#include <eng/common/handle.hpp>

namespace eng
{
namespace gfx
{

struct Shader
{
    auto operator==(const Shader& o) const { return path == o.path; }
    std::filesystem::path path;
    ShaderStage stage{ ShaderStage::NONE };
    void* metadata{};
};

enum class CullFace
{
    NONE,
    FRONT,
    BACK,
    FRONT_AND_BACK,
};

enum class VertexFormat
{
    R32_SFLOAT,
    R32G32_SFLOAT,
    R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT,
};

enum class DepthCompare
{
    NEVER,
    LESS,
    GREATER,
    EQUAL
};

enum class PolygonMode
{
    FILL
};

enum class StencilOp
{
    KEEP,
    ZERO,
    REPLACE,
    INCREMENT_AND_CLAMP,
    DECREMENT_AND_CLAMP,
    INVERT,
    INCREMENT_AND_WRAP,
    DECREMENT_AND_WRAP,
};

enum class CompareOp
{
    NEVER,
    LESS,
    EQUAL,
    LESS_OR_EQUAL,
    GREATER,
    NOT_EQUAL,
    GREATER_OR_EQUAL,
    ALWAYS,
};

enum class BlendFactor
{
    ZERO,
    ONE,
    SRC_COLOR,
    ONE_MINUS_SRC_COLOR,
    DST_COLOR,
    ONE_MINUS_DST_COLOR,
    SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA,
    DST_ALPHA,
    ONE_MINUS_DST_ALPHA,
    CONSTANT_COLOR,
    ONE_MINUS_CONSTANT_COLOR,
    CONSTANT_ALPHA,
    ONE_MINUS_CONSTANT_ALPHA,
    SRC_ALPHA_SATURATE,
};

enum class BlendOp
{
    ADD,
    SUBTRACT,
    REVERSE_SUBTRACT,
    MIN,
    MAX,
};

struct PipelineCreateInfo
{
    struct VertexBinding
    {
        auto operator<=>(const VertexBinding&) const = default;
        uint32_t binding;
        uint32_t stride;
        bool instanced{ false };
    };

    struct VertexAttribute
    {
        auto operator<=>(const VertexAttribute&) const = default;
        uint32_t location;
        uint32_t binding;
        VertexFormat format{};
        uint32_t offset;
    };

    struct StencilState
    {
        auto operator<=>(const StencilState&) const = default;
        StencilOp fail;
        StencilOp pass;
        StencilOp depth_fail;
        CompareOp compare;
        uint32_t compare_mask{ ~0u };
        uint32_t write_mask{ ~0u };
        uint32_t ref{};
    };

    struct BlendState
    {
        auto operator<=>(const BlendState&) const = default;
        bool enable{ false };
        BlendFactor src_color_factor{};
        BlendFactor dst_color_factor{};
        BlendOp color_op{};
        BlendFactor src_alpha_factor{};
        BlendFactor dst_alpha_factor{};
        BlendOp alpha_op{};
        uint32_t r : 1 { 1 };
        uint32_t g : 1 { 1 };
        uint32_t b : 1 { 1 };
        uint32_t a : 1 { 1 };
    };

    struct AttachmentState
    {
        auto operator==(const AttachmentState& o) const
        {
            if(count != o.count) { return false; }
            if(depth_format != o.depth_format) { return false; }
            for(auto i = 0u; i < count; ++i)
            {
                if(color_formats.at(i) != o.color_formats.at(i) || blend_states.at(i) != o.blend_states.at(i))
                {
                    return false;
                }
            }
            return true;
        }

        uint32_t count{};
        std::array<ImageFormat, 8> color_formats{};
        std::array<BlendState, 8> blend_states{};
        ImageFormat depth_format{};
        ImageFormat stencil_format{};
    };

    bool operator==(const PipelineCreateInfo& a) const = default;

    std::vector<Handle<Shader>> shaders;
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;

    AttachmentState attachments;
    bool depth_test{ false };
    bool depth_write{ false };
    DepthCompare depth_compare{ DepthCompare::NEVER };
    bool stencil_test{ false };
    StencilState stencil_front;
    StencilState stencil_back;

    PolygonMode polygon_mode{ PolygonMode::FILL };
    CullFace culling{ CullFace::NONE };
    bool front_is_ccw{ true };
    float line_width{ 1.0f };
};

struct Pipeline
{
    bool operator==(const Pipeline& a) const { return info == a.info; }
    PipelineCreateInfo info;
    void* metadata{};
};

} // namespace gfx
} // namespace eng

DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::VertexBinding, eng::hash::combine_fnv1a(t.binding, t.stride, t.instanced));
DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::VertexAttribute, eng::hash::combine_fnv1a(t.location, t.binding, t.format, t.offset));
DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::StencilState,
                eng::hash::combine_fnv1a(t.fail, t.pass, t.depth_fail, t.compare, t.compare_mask, t.write_mask, t.ref));
DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::BlendState,
                eng::hash::combine_fnv1a(t.enable, t.src_color_factor, t.dst_color_factor, t.color_op,
                                         t.src_alpha_factor, t.dst_alpha_factor, t.alpha_op, t.r, t.g, t.b, t.a));
DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::AttachmentState, [&t] {
    uint64_t hash = 0;
    // clang-format off
    for(auto i=0u; i<t.count; ++i) { hash = eng::hash::combine_fnv1a(hash, t.color_formats.at(i)); }
    for(auto i=0u; i<t.count; ++i) { hash = eng::hash::combine_fnv1a(hash, t.blend_states.at(i)); }
    // clang-format on
    hash = eng::hash::combine_fnv1a(hash, t.count, t.depth_format, t.stencil_format);
    return hash;
}());
DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo, [&t] {
    uint64_t hash = 0;
    // clang-format off
    for(const auto& e : t.shaders) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.bindings) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.attributes) { hash = eng::hash::combine_fnv1a(hash, e); }
    // clang-format on
    hash = eng::hash::combine_fnv1a(hash, t.attachments, t.depth_test, t.depth_write, t.depth_compare, t.stencil_test,
                                    t.stencil_front, t.stencil_back, t.polygon_mode, t.culling, t.front_is_ccw, t.line_width);
    return hash;
}());
DEFINE_STD_HASH(eng::gfx::Pipeline, eng::hash::combine_fnv1a(t.info));
DEFINE_STD_HASH(eng::gfx::Shader, eng::hash::combine_fnv1a(t.path));

ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Shader);