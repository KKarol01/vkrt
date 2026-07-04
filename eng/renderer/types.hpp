#pragma once

// Windows macro. Expands to '2'. Don't need it, compiles without it. Thanks MS.
#ifdef OPAQUE
#undef OPAQUE
#endif
// Windows macro. Expands to '1'. Don't need it, compiles without it. Thanks MS.
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif

#include <array>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

// Enums
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
    LEQUAL,
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
    // MESHLETIZE_BIT = 0x1,
    DIRTY_BLAS_BIT = 0x2,
};

enum class IndexFormat
{
    INVALID,
    U8,
    U16,
    U32,
};

enum class VertexComponent : u32
{
    NONE = 0x0,
    POSITION_BIT = 0x1,
    NORMAL_BIT = 0x2,
    TANGENT_BIT = 0x4,
    UV0_BIT = 0x8,
    ALL = POSITION_BIT | NORMAL_BIT | TANGENT_BIT | UV0_BIT
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
    AS_BUILD_INPUT = 0x40,
    AS_STORAGE = 0x80,
    AS_SCRATCH = 0x100,
};
ENG_ENABLE_FLAGS_OPERATORS(BufferUsage);

enum class ImageFormat : u8
{
    UNDEFINED,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    D16_UNORM,
    D24_S8_UNORM,
    D24_X8_TYPELESS_UNORM,
    D32_SFLOAT,
    R16F,
    R32F,
    R16FG16F,
    R32FG32F,
    R16FG16FB16FA16F,
    R32FG32FB32FA32F,
    LAST_ENUM,
};

enum class ImageAspect : u8
{
    NONE = 0x0,
    COLOR = 0x1,
    DEPTH = 0x2,
    STENCIL = 0x4,
    DEPTH_STENCIL = DEPTH | STENCIL,

    /* Remember about get_aspect_from_format when adding new formats. */
};
ENG_ENABLE_FLAGS_OPERATORS(ImageAspect);

enum class ImageUsage : u32
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

enum class ImageLayout : u8
{
    UNDEFINED,
    GENERAL,
    READ_ONLY,
    ATTACHMENT,
    TRANSFER_SRC,
    TRANSFER_DST,
    PRESENT,

    LAST_ENUM,
};

enum class ImageType : u8
{
    TYPE_1D,
    TYPE_2D,
    TYPE_3D,
};

enum class ImageViewType : u8
{
    NONE,
    TYPE_1D,
    TYPE_2D,
    TYPE_3D,
};

enum class ChannelSwizzle : u8
{
    IDENTITY,
    ZERO,
    ONE,
    R,
    G,
    B,
    A
};

enum class ImageFilter : u8
{
    NEAREST,
    LINEAR,
};

enum class ImageAddressing : u8
{
    REPEAT,
    CLAMP_EDGE
};

enum class MeshPassType : u8
{
    Z_PREPASS,
    OPAQUE,
    DIRECTIONAL_SHADOW,
    WIREFRAME,
    LAST_ENUM,
};

enum class PipelineStage : u16
{
    NONE = 0x0,
    TRANSFER_BIT = 0x1,
    VERTEX_BIT = 0x2,
    FRAGMENT_BIT = 0x4,
    EARLY_Z_BIT = 0x8,
    LATE_Z_BIT = 0x10,
    COLOR_OUT_BIT = 0x20,
    COMPUTE_BIT = 0x40,
    INDIRECT_BIT = 0x80,
    VERTEX_INPUT_BIT = 0x100,
    ALL = 0x200, // vulkan uses special value; it's not all bits set to 1

    AS_BUILD_BIT = 0x200,
    RAY_TRACING_BIT = 0x400,
};
ENG_ENABLE_FLAGS_OPERATORS(PipelineStage);

enum class PipelineAccess : u32
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
    AS_READ_BIT = 0x800,
    AS_WRITE_BIT = 0x1000,
    AS_RW = AS_READ_BIT | AS_WRITE_BIT,
    PRESENT_BIT = 0x2000,
    INDEX_READ_BIT = 0x4000,

    READS = SHADER_READ_BIT | COLOR_READ_BIT | DS_READ_BIT | STORAGE_READ_BIT | INDIRECT_READ_BIT | TRANSFER_READ_BIT |
            AS_READ_BIT | PRESENT_BIT | INDEX_READ_BIT,
    WRITES = SHADER_WRITE_BIT | COLOR_WRITE_BIT | DS_WRITE_BIT | STORAGE_WRITE_BIT | TRANSFER_WRITE_BIT | AS_WRITE_BIT,

    LAST_ENUM,
};
ENG_ENABLE_FLAGS_OPERATORS(PipelineAccess)

enum class ShaderStage : u32
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

enum class DescriptorType
{
    UNDEFINED,
    STORAGE_BUFFER,
    SAMPLED_IMAGE,
    STORAGE_IMAGE,
    SEPARATE_SAMPLER,
    ACCELERATION_STRUCTURE,
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
    NONE,
    MIN,
    MAX
};

enum class SamplerMipmapMode
{
    NEAREST,
    LINEAR,
};

enum class AllocateMemory : u8
{
    ALIASED,  // Gpu memory allocator will not create unique memory for this resource.
    EXTERNAL, // Memory is managed by someone else (swapchain images).
    YES,
};

enum class Compilation : u8
{
    NOW,     // resources get compiled now
    DEFERRED // resources are put in a queue and batch-compiled
};

enum class AOMode
{
    SSAO,
    GTAO,
    RTAO,
    SSILVB,
    LAST_ENUM,
};

enum class QueryType : u8
{
    NONE,
    TIMESTAMP,
    OCCLUSION,
    PERFORMANCE,
};

enum class DiscardContents : u8
{
    NO,
    YES,
};

// Forward declarations
class BindlessPool;
class CommandBufferVk;
class CommandPoolVk;
class ICommandBuffer;
class ICommandPool;
class ImGuiRenderer;
class Renderer;
class RGRenderGraph;
class StagingBuffer;
class SubmitQueue;
enum class SyncType;
struct BLASInstanceSettings;
struct Buffer;
struct BufferMetadataVk;
struct BufferView;
struct DebugGeometry;
struct DescriptorLayout;
struct DescriptorLayoutMetadataVk;
struct DescriptorResource;
struct DescriptorSet;
struct DescriptorSetMetadataVk;
struct Geometry;
struct GeometryDescriptor;
struct GeometryMetadataVk;
struct IDescriptorSetAllocator;
struct Image;
struct ImageMetadataVk;
struct ImageMipsLayers;
struct ImageView;
struct ImageViewMetadataVk;
struct InstanceSettings;
struct Material;
struct Mesh;
struct MeshDescriptor;
struct Meshlet;
struct MeshPass;
struct Pipeline;
struct PipelineLayout;
struct PipelineLayoutMetadataVk;
struct PipelineMetadataVk;
struct PushRange;
struct QueryPool;
struct QueryPoolCreateInfo;
struct QueryPoolMetadataVk;
struct RGAccess;
struct RGBuilder;
struct RGResource;
struct Sampler;
struct SamplerMetadataVk;
struct ScopedTimestampQuery;
struct Shader;
struct ShaderEffect;
struct ShaderMetadataVk;
struct Swapchain;
struct Sync;
struct SyncCreateInfo;
struct Texture;
struct TimestampQuery;
struct VkDescriptorPoolMetadata;

} // namespace gfx

} // namespace eng

// Shared types (to avoid including renderer.hpp)
namespace eng
{
namespace gfx
{
struct BufferView
{
    auto operator<=>(const BufferView&) const = default;
    explicit operator bool() const { return (bool)buffer; }
    static BufferView init(Handle<Buffer> buffer, size_t start = 0, size_t size = ~0ull)
    {
        return BufferView{ .buffer = buffer, .range = { start, size } };
    }
    Handle<Buffer> buffer;
    Range64u range{};
};

struct ImageView
{
    ENG_SERIALIZATION_STRUCT_VERSION(0);
    static ImageView init(Handle<Image> image, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                          u32 src_mip = 0u, u32 dst_mip = ~0u, u32 src_layer = 0u, u32 dst_layer = ~0u);
    auto operator<=>(const ImageView&) const = default;
    explicit operator bool() const { return (bool)image; }
    void* get_md();
    const void* get_md() const;
    Handle<Image> image;
    ImageViewType type{};
    ImageFormat format{};
    u32 src_subresource{};
    u32 dst_subresource{};
};

// Helper funcs
inline constexpr size_t get_vertex_component_size(VertexComponent comp)
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
    ENG_ASSERT("Undefined case");
    return 0;
}

inline constexpr size_t get_vertex_layout_size(Flags<VertexComponent> layout)
{
    static_assert((int)VertexComponent::ALL == 15);
    static constexpr VertexComponent comps[]{ VertexComponent::POSITION_BIT, VertexComponent::NORMAL_BIT,
                                              VertexComponent::TANGENT_BIT, VertexComponent::UV0_BIT };
    size_t sum = 0;
    for(auto e : std::span{ comps })
    {
        if(layout.test(e)) { sum += get_vertex_component_size(e); }
    }
    return sum;
}

inline constexpr size_t get_vertex_component_offset(Flags<VertexComponent> layout, VertexComponent comp)
{
    return get_vertex_layout_size(layout & (std::to_underlying(comp) - 1)); // calc size of all previous present components in the bitmask
}

inline constexpr size_t get_vertex_component_offset(VertexComponent comp)
{
    return get_vertex_component_offset(Flags<VertexComponent>{ VertexComponent::ALL }, comp);
}

inline constexpr size_t get_vertex_count(std::span<const float> vertices, Flags<VertexComponent> layout)
{
    return vertices.size_bytes() / get_vertex_layout_size(layout);
}

inline constexpr size_t get_index_size(IndexFormat format)
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
    ENG_ASSERT("Undefined case");
    return 0;
}

inline constexpr size_t get_index_count(std::span<const std::byte> indices, IndexFormat format)
{
    return indices.size() / get_index_size(format);
}

// Copies indices between formats. Dstformat must be greater or equal to srcf in byte size.
// If dst is empty, only returns number of indices in the source span, so that it can be resized
// and this function be called again.
// Returns number of copied indices.
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
    else if(dstf == IndexFormat::U32 && srcf == IndexFormat::U16) { copy_over.template operator()<u32, u16>(); }
    else if(dstf == IndexFormat::U32 && srcf == IndexFormat::U8) { copy_over.template operator()<u32, u8>(); }
    else if(dstf == IndexFormat::U16 && srcf == IndexFormat::U16) { memcpy(dst.data(), src.data(), src.size_bytes()); }
    else if(dstf == IndexFormat::U16 && srcf == IndexFormat::U8) { copy_over.template operator()<u16, u8>(); }
    else if(dstf == IndexFormat::U8 && srcf == IndexFormat::U8) { memcpy(dst.data(), src.data(), src.size_bytes()); }
    else
    {
        ENG_ASSERT("Unhandled case.");
        return 0;
    }
    return ic;
}

inline Flags<ImageAspect> get_aspect_from_format(ImageFormat format)
{
    static_assert((int)ImageFormat::LAST_ENUM == 13);
    switch(format)
    {
    case ImageFormat::R8G8B8A8_UNORM:
    case ImageFormat::R8G8B8A8_SRGB:
    case ImageFormat::R16F:
    case ImageFormat::R32F:
    case ImageFormat::R16FG16F:
    case ImageFormat::R32FG32F:
    case ImageFormat::R16FG16FB16FA16F:
    case ImageFormat::R32FG32FB32FA32F:
        return ImageAspect::COLOR;

    case ImageFormat::D16_UNORM:
    case ImageFormat::D24_X8_TYPELESS_UNORM:
    case ImageFormat::D32_SFLOAT:
        return ImageAspect::DEPTH;

    case ImageFormat::D24_S8_UNORM:
        return ImageAspect::DEPTH_STENCIL;

    default:
    {
        ENG_ASSERT("Undefined case");
        return ImageAspect::NONE;
    }
    }
}

inline ImageViewType get_view_type_from_image(ImageType type)
{
    switch(type)
    {
    case ImageType::TYPE_1D:
        return ImageViewType::TYPE_1D;
    case ImageType::TYPE_2D:
        return ImageViewType::TYPE_2D;
    case ImageType::TYPE_3D:
        return ImageViewType::TYPE_3D;
    default:
    {
        ENG_ASSERT("Undefined case");
        return ImageViewType::NONE;
    }
    }
}
} // namespace gfx
} // namespace eng

namespace eng
{
namespace serialization
{
template <> inline constexpr auto get_struct_fields<gfx::ImageView>()
{
    ENG_SERIALIZATION_STRUCT_VERSION_CHECK(gfx::ImageView, 0);
    return std::make_tuple(ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(gfx::ImageView, image),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(gfx::ImageView, type),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(gfx::ImageView, format),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(gfx::ImageView, src_subresource),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(gfx::ImageView, dst_subresource));
}
} // namespace serialization
} // namespace eng

ENG_DEFINE_STD_HASH(eng::gfx::ImageView, ENG_HASH(t.image, t.type, t.format, t.src_subresource, t.dst_subresource));
ENG_DEFINE_STD_HASH(eng::gfx::BufferView, ENG_HASH(t.buffer, t.range));