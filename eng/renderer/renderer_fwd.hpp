#pragma once

#include <cstdint>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>

namespace eng
{
namespace gfx
{
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
    GEQUAL,
    EQUAL
};

enum class Topology
{
    LINE_LIST,
    TRIANGLE_LIST,
};

enum class PolygonMode
{
    FILL,
    LINE,
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

enum class PipelineType
{
    NONE,
    GRAPHICS,
    COMPUTE,
    RAYTRACING,
};

enum class GeometryFlags
{
    DIRTY_BLAS_BIT = 0x1,
};

enum class IndexFormat
{
    U8,
    U16,
    U32,
};

enum class VertexComponent : uint32_t
{
    NONE = 0x0,
    POSITION_BIT = 0x1,
    NORMAL_BIT = 0x2,
    TANGENT_BIT = 0x4,
    UV0_BIT = 0x8,
};
ENG_ENABLE_FLAGS_OPERATORS(VertexComponent);

enum class InstanceFlags
{
    RAY_TRACED_BIT = 0x1
};

enum class BufferUsage
{
    NONE = 0x0,
    INDEX_BIT = 0x1,
    STORAGE_BIT = 0x2,
    INDIRECT_BIT = 0x4,
    TRANSFER_SRC_BIT = 0x8,
    TRANSFER_DST_BIT = 0x10,
    CPU_ACCESS = 0x20,
};
ENG_ENABLE_FLAGS_OPERATORS(BufferUsage);

enum class ImageFormat
{
    UNDEFINED,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    D16_UNORM,
    D24_S8_UNORM,
    D32_SFLOAT,
    R16F,
    R32F,
    R32FG32FB32FA32F,
};

enum class ImageAspect
{
    NONE,
    COLOR,
    DEPTH,
    STENCIL,
    DEPTH_STENCIL,
};

enum class ImageUsage
{
    NONE = 0x0,
    STORAGE_BIT = 0x1,
    SAMPLED_BIT = 0x2,
    TRANSFER_SRC_BIT = 0x4,
    TRANSFER_DST_BIT = 0x8,
    TRANSFER_RW = TRANSFER_SRC_BIT | TRANSFER_DST_BIT,
    COLOR_ATTACHMENT_BIT = 0x10,
    DEPTH_BIT = 0x20,
    STENCIL_BIT = 0x40,
    DS = DEPTH_BIT | STENCIL_BIT,
};
ENG_ENABLE_FLAGS_OPERATORS(ImageUsage);

enum class ImageLayout
{
    UNDEFINED = 0x0,
    GENERAL = 0x1,
    READ_ONLY = 0x2,
    ATTACHMENT = 0x4,
    TRANSFER_SRC = 0x8,
    TRANSFER_DST = 0x10,
    PRESENT = 0x20,
};

enum class ImageType
{
    TYPE_1D,
    TYPE_2D,
    TYPE_3D,
};

enum class ImageViewType
{
    NONE,
    TYPE_1D,
    TYPE_2D,
    TYPE_3D,
};

enum class ImageFilter
{
    NEAREST,
    LINEAR,
};

enum class ImageAddressing
{
    REPEAT,
    CLAMP_EDGE
};

enum class RenderPassType : uint32_t
{
    FORWARD,
    DIRECTIONAL_SHADOW,
    LAST_ENUM,
};

enum class PipelineStage : uint32_t
{
    NONE = 0x0,
    ALL = 0xFFFFFFFF,
    TRANSFER_BIT = 0x1,
    VERTEX_BIT = 0x2,
    FRAGMENT = 0x4,
    EARLY_Z_BIT = 0x8,
    LATE_Z_BIT = 0x10,
    COLOR_OUT_BIT = 0x20,
    COMPUTE_BIT = 0x40,
    INDIRECT_BIT = 0x80,
};
ENG_ENABLE_FLAGS_OPERATORS(PipelineStage);

enum class PipelineAccess : uint32_t
{
    NONE = 0x0,
    SHADER_READ_BIT = 0x1,
    SHADER_WRITE_BIT = 0x2,
    SHADER_RW = SHADER_READ_BIT | SHADER_WRITE_BIT,
    COLOR_READ_BIT = 0x4,
    COLOR_WRITE_BIT = 0x8,
    COLOR_RW_BIT = COLOR_READ_BIT | COLOR_WRITE_BIT,
    DS_READ_BIT = 0x10,
    DS_WRITE_BIT = 0x20,
    DS_RW = DS_READ_BIT | DS_WRITE_BIT,
    STORAGE_READ_BIT = 0x40,
    STORAGE_WRITE_BIT = 0x80,
    STORAGE_RW = STORAGE_READ_BIT | STORAGE_WRITE_BIT,
    INDIRECT_READ_BIT = 0x100,
    TRANSFER_READ_BIT = 0x200,
    TRANSFER_WRITE_BIT = 0x400,
    TRANSFER_RW = TRANSFER_READ_BIT | TRANSFER_WRITE_BIT,
};
ENG_ENABLE_FLAGS_OPERATORS(PipelineAccess)

enum class ShaderStage : uint32_t
{
    NONE = 0x0,
    ALL = 0xFFFFFFFF,
    VERTEX_BIT = 0x1,
    PIXEL_BIT = 0x2,
    COMPUTE_BIT = 0x4,
    RAYGEN_BIT = 0x8,
    ANY_HIT_BIT = 0x10,
    CLOSEST_HIT_BIT = 0x20,
    MISS_BIT = 0x40,
    INTERSECTION_BIT = 0x80,
};
ENG_ENABLE_FLAGS_OPERATORS(ShaderStage);

enum class PipelineSetFlags : uint32_t
{
    UPDATE_AFTER_BIND_BIT = 0x1,
};
ENG_ENABLE_FLAGS_OPERATORS(PipelineSetFlags);

enum class PipelineBindingFlags : uint32_t
{
    UPDATE_AFTER_BIND_BIT = 0x1,
    UPDATE_UNUSED_WHILE_PENDING_BIT = 0x2,
    PARTIALLY_BOUND_BIT = 0x4,
};
ENG_ENABLE_FLAGS_OPERATORS(PipelineBindingFlags);

enum class PipelineBindingType
{
    UNDEFINED,
    STORAGE_BUFFER,
    SAMPLED_IMAGE,
    STORAGE_IMAGE,
    SEPARATE_SAMPLER,
};

enum class DescriptorPoolFlags
{
    UPDATE_AFTER_BIND_BIT = 0x1,
};
ENG_ENABLE_FLAGS_OPERATORS(DescriptorPoolFlags);

enum class QueueType
{
    GRAPHICS,
    COPY,
    COMPUTE,
};

enum class SamplerReductionMode
{
    MIN,
    MAX
};

enum class SamplerMipmapMode
{
    NEAREST,
    LINEAR,
};

struct ImageBlockData;
struct Shader;
struct PipelineLayoutCreateInfo;
struct DescriptorPoolCreateInfo;
struct DescriptorPool;
struct DescriptorSet;
struct PipelineLayout;
struct PipelineCreateInfo;
struct Pipeline;
struct Geometry;
struct ShaderEffect;
struct MeshPassCreateInfo;
struct MeshPass;
struct Material;
struct Mesh;
struct Meshlet;
struct GeometryDescriptor;
struct BufferDescriptor;
struct Buffer;
struct ImageDescriptor;
struct Image;
struct ImageViewDescriptor;
struct ImageView;
struct ImageSubRange;
struct ImageSubLayers;
struct ImageBlit;
struct ImageCopy;
struct SamplerDescriptor;
struct Sampler;
struct TextureDescriptor;
struct Texture;
struct MaterialDescriptor;
struct MeshDescriptor;
struct InstanceSettings;
struct BLASInstanceSettings;
struct VsmData;
struct Swapchain;
struct DebugGeometry;

class CommandBuffer;
class CommandPool;
enum class SyncType;
struct SyncCreateInfo;
struct Sync;
class SubmitQueue;

inline size_t get_vertex_component_size(VertexComponent comp)
{
    switch(comp)
    {
    case VertexComponent::NONE:
        return 0;
    case VertexComponent::POSITION_BIT:
        return 3 * sizeof(float);
    case VertexComponent::NORMAL_BIT:
        return 3 * sizeof(float);
    case VertexComponent::TANGENT_BIT:
        return 4 * sizeof(float);
    case VertexComponent::UV0_BIT:
        return 2 * sizeof(float);
    }
    ENG_ERROR("Undefined case");
    return 0;
}

inline size_t get_vertex_layout_size(Flags<VertexComponent> layout)
{
    static constexpr VertexComponent comps[]{ VertexComponent::POSITION_BIT, VertexComponent::NORMAL_BIT,
                                              VertexComponent::TANGENT_BIT, VertexComponent::UV0_BIT };
    size_t sum = 0;
    for(auto e : std::span{ comps })
    {
        if(layout.test_clear(e)) { sum += get_vertex_component_size(e); }
    }
    assert(layout.empty());
    return sum;
}

inline size_t get_vertex_component_offset(Flags<VertexComponent> layout, VertexComponent comp)
{
    return get_vertex_layout_size(layout & (std::to_underlying(comp) - 1)); // calc size of all previous present components in the bitmask
}

inline size_t get_vertex_count(std::span<const float> vertices, Flags<VertexComponent> layout)
{
    return vertices.size_bytes() / get_vertex_layout_size(layout);
}

inline size_t get_index_size(IndexFormat format)
{
    switch(format)
    {
    case IndexFormat::U8:
        return 1;
    case IndexFormat::U16:
        return 2;
    case IndexFormat::U32:
        return 4;
    }
    ENG_ERROR("Undefined case");
    return 0;
}

inline size_t get_index_count(std::span<const std::byte> indices, IndexFormat format)
{
    return indices.size() / get_index_size(format);
}

// Copies indices between formats. Dstformat must be greater or equal to srcf in byte size.
// If dst is empty, only returns number of indices in the source span, so that it can be resized
// and this function be called again.
// Returns number of indices.
inline size_t copy_indices(std::span<std::byte> dst, std::span<const std::byte> src, IndexFormat dstf, IndexFormat srcf)
{
    if(src.empty()) { return 0; }
    const auto ic = get_index_count(src, srcf);
    if(dst.empty()) { return ic; }
    const auto dststride = get_index_size(dstf);
    const auto srcstride = get_index_size(srcf);
    const auto copy_over = [&dst, &src, ic]<typename DstType, typename SrcType> {
        auto* pdst = (DstType*)dst.data();
        auto* psrc = (SrcType*)src.data();
        for(auto i = 0ull; i < ic; ++i)
        {
            pdst[i] = static_cast<DstType>(psrc[i]);
        }
    };
    if(dstf == IndexFormat::U32 && srcf == IndexFormat::U32) { memcpy(dst.data(), src.data(), src.size_bytes()); }
    else if(dstf == IndexFormat::U32 && srcf == IndexFormat::U16)
    {
        copy_over.template operator()<uint32_t, uint16_t>();
    }
    else if(dstf == IndexFormat::U32 && srcf == IndexFormat::U8) { copy_over.template operator()<uint32_t, uint8_t>(); }
    else if(dstf == IndexFormat::U16 && srcf == IndexFormat::U16) { memcpy(dst.data(), src.data(), src.size_bytes()); }
    else if(dstf == IndexFormat::U16 && srcf == IndexFormat::U8) { copy_over.template operator()<uint16_t, uint8_t>(); }
    else if(dstf == IndexFormat::U8 && srcf == IndexFormat::U8) { memcpy(dst.data(), src.data(), src.size_bytes()); }
    else
    {
        ENG_ERROR("Unhandled case.");
        return 0;
    }
    return ic;
}

} // namespace gfx
} // namespace eng

ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Buffer);
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Image);
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Sampler);
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Geometry);
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Mesh);
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::DescriptorPool);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Shader);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::ImageView);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Texture);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Material);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::PipelineLayout);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Pipeline);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::MeshPass);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::ShaderEffect);