#pragma once

#include <cstdint>
#include <span>
#include <compare>
#include <utility>
#include <array>
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/common/hash.hpp>

namespace eng
{
namespace gfx
{

struct Buffer;
struct Image;
struct ImageView;
struct Sampler;
struct Pipeline;
struct Texture;

enum class GeometryFlags
{
    DIRTY_BLAS_BIT = 0x1,
};

enum class InstanceFlags
{
    RAY_TRACED_BIT = 0x1
};

enum class ImageFormat
{
    UNDEFINED,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    D16_UNORM,
    D24_S8_UNORM,
    D32_SFLOAT,
    R16F,
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
    DEPTH_STENCIL_ATTACHMENT_BIT = 0x20,
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

enum class MeshPassType
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
    EARLY_Z_BIT = 0x2,
    LATE_Z_BIT = 0x4,
    COLOR_OUT_BIT = 0x8,
    COMPUTE_BIT = 0x10,
    INDIRECT_BIT = 0x20,
};
using PipelineStageFlags = Flags<PipelineStage>;
ENG_ENABLE_FLAGS_OPERATORS(PipelineStage);

enum class PipelineAccess : uint32_t
{
    NONE = 0x0,
    SHADER_READ_BIT = 0x1,
    SHADER_WRITE_BIT = 0x2,
    SHADER_RW = SHADER_READ_BIT | SHADER_WRITE_BIT,
    COLOR_READ_BIT = 0x4,
    COLOR_WRITE_BIT = 0x8,
    DS_READ_BIT = 0x10,
    DS_WRITE_BIT = 0x20,
    DS_RW = DS_READ_BIT | DS_WRITE_BIT,
    STORAGE_READ_BIT = 0x40,
    STORAGE_WRITE_BIT = 0x80,
    INDIRECT_READ_BIT = 0x100,
    TRANSFER_READ_BIT = 0x200,
    TRANSFER_WRITE_BIT = 0x400,
    TRANSFER_RW = TRANSFER_READ_BIT | TRANSFER_WRITE_BIT,
};
using PipelineAccessFlags = Flags<PipelineAccess>;
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
using ShaderStageFlags = Flags<ShaderStage>;
ENG_ENABLE_FLAGS_OPERATORS(ShaderStage);

struct Geometry
{
    auto operator<=>(const Geometry& a) const = default;
    Range vertex_range{};  // position inside vertex buffer
    Range index_range{};   // position inside index buffer
    Range meshlet_range{}; // position inside meshlet buffer
    // VkAccelerationStructureKHR blas{};
    // Handle<Buffer> blas_buffer{};
};

struct ShaderEffect
{
    auto operator<=>(const ShaderEffect&) const = default;
    Handle<Pipeline> pipeline;
};

struct MeshPassCreateInfo
{
    std::string name;
    std::array<Handle<ShaderEffect>, (uint32_t)MeshPassType::LAST_ENUM> effects;
};

struct MeshPass
{
    bool operator==(const MeshPass& o) const { return name == o.name; }
    std::string name;
    std::array<Handle<ShaderEffect>, (uint32_t)MeshPassType::LAST_ENUM> effects;
};

struct Material
{
    auto operator<=>(const Material& t) const = default;
    Handle<MeshPass> mesh_pass;
    Handle<Texture> base_color_texture;
};

struct Mesh
{
    auto operator<=>(const Mesh& t) const = default;
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Meshlet
{
    uint32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t index_offset;
    uint32_t index_count;
    glm::vec4 bounding_sphere{};
};

struct GeometryDescriptor
{
    Flags<GeometryFlags> flags;
    std::span<const Vertex> vertices;
    std::span<uint32_t> indices;
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

struct BufferDescriptor
{
    std::string name;
    size_t size{};
    Flags<BufferUsage> usage{};
};

struct Buffer
{
    constexpr Buffer() noexcept = default;
    explicit Buffer(const BufferDescriptor& info) noexcept : name(info.name), usage(info.usage), capacity(info.size) {}
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = default;
    Buffer& operator=(Buffer&&) = default;

    std::string name;
    Flags<BufferUsage> usage{};
    size_t capacity{};
    size_t size{};
    void* metadata{};
    void* memory{};
};

struct ImageDescriptor
{
    std::string name;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1 };
    uint32_t mips{ 1 };
    ImageFormat format{ ImageFormat::R8G8B8A8_UNORM };
    ImageType type{ ImageType::TYPE_2D };
    Flags<ImageUsage> usage{};
    std::span<const std::byte> data;
};

struct Image
{
    Image() noexcept = default;
    explicit Image(const ImageDescriptor& info) noexcept
        : name(info.name), width(info.width), height(info.height), depth(info.depth), mips(info.mips),
          format(info.format), type(info.type), usage(info.usage)
    {
    }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) = default;
    Image& operator=(Image&&) = default;

    ImageViewType deduce_view_type() const
    {
        return type == ImageType::TYPE_1D   ? ImageViewType::TYPE_1D
               : type == ImageType::TYPE_2D ? ImageViewType::TYPE_2D
               : type == ImageType::TYPE_3D ? ImageViewType::TYPE_3D
                                            : ImageViewType::NONE;
    }

    ImageAspect deduce_aspect() const
    {
        if(format == ImageFormat::D16_UNORM || format == ImageFormat::D32_SFLOAT) { return ImageAspect::DEPTH; }
        if(format == ImageFormat::D24_S8_UNORM) { return ImageAspect::DEPTH_STENCIL; }
        return ImageAspect::COLOR;
    }

    std::string name;
    ImageType type{ ImageType::TYPE_2D };
    ImageFormat format{};
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1u };
    uint32_t mips{ 1u };
    uint32_t layers{ 1u };
    Flags<ImageUsage> usage{ ImageUsage::NONE };
    ImageLayout current_layout{ ImageLayout::UNDEFINED };
    Handle<ImageView> default_view;
    void* metadata{};
};

struct ImageViewDescriptor
{
    std::string name;
    Handle<Image> image;
    std::optional<ImageViewType> view_type;
    std::optional<ImageFormat> format;
    std::optional<ImageAspect> aspect;
    Range32 mips{ 0, ~0u };
    Range32 layers{ 0, ~0u };
    // swizzle always identity for now
};

struct ImageView
{
    bool operator==(const ImageView& a) const
    {
        return image == a.image && type == a.type && format == a.format && aspect == a.aspect && mips == a.mips &&
               layers == a.layers;
    }
    std::string name;
    Handle<Image> image;
    ImageViewType type{};
    ImageFormat format{};
    ImageAspect aspect{};
    Range32 mips{};
    Range32 layers{};
    void* metadata{};
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

struct SamplerDescriptor
{
    auto operator<=>(const SamplerDescriptor& a) const = default;
    std::array<ImageFilter, 2> filtering{ ImageFilter::LINEAR, ImageFilter::LINEAR }; // [min, mag]
    std::array<ImageAddressing, 3> addressing{ ImageAddressing::REPEAT, ImageAddressing::REPEAT, ImageAddressing::REPEAT }; // u, v, w
    std::array<float, 3> mip_lod{ 0.0f, 1000.0f, 0.0f }; // min, max, bias
    SamplerMipmapMode mipmap_mode{ SamplerMipmapMode::LINEAR };
    std::optional<SamplerReductionMode> reduction_mode{};
};

struct Sampler
{
    auto operator==(const Sampler& a) const { return info == a.info; }
    SamplerDescriptor info;
    void* metadata{};
};

struct TextureDescriptor
{
    Handle<ImageView> view;
    Handle<Sampler> sampler;
    ImageLayout layout;
};

struct Texture
{
    auto operator<=>(const Texture& t) const = default;
    Handle<ImageView> view;
    Handle<Sampler> sampler;
    ImageLayout layout{ ImageLayout::READ_ONLY };
};

struct MaterialDescriptor
{
    std::string mesh_pass{ "default_unlit" };
    Handle<Texture> base_color_texture;
    Handle<Texture> normal_texture;
    Handle<Texture> metallic_roughness_texture;
};

struct MeshDescriptor
{
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct InstanceSettings
{
    ecs::entity entity;
};

struct BLASInstanceSettings
{
    ecs::entity entity;
};

struct VsmData
{
    Handle<Buffer> constants_buffer;
    Handle<Buffer> free_allocs_buffer;
    Handle<Image> shadow_map_0;
    // VkImageView view_shadow_map_0_general{};
    Handle<Image> dir_light_page_table;
    // VkImageView view_dir_light_page_table_general{};
    Handle<Image> dir_light_page_table_rgb8;
    // VkImageView view_dir_light_page_table_rgb8_general{};
};

struct Swapchain
{
    explicit Swapchain(uint32_t num_images);
    uint32_t acquire(VkResult* res, uint64_t timeout = -1ull, Sync* semaphore = nullptr, Sync* = nullptr);
    Handle<Image> get_current_image();
    Handle<ImageView> get_current_view();
    void* metadata{};
    std::vector<Handle<Image>> images;
    std::vector<Handle<ImageView>> views;
    uint32_t current_index{ 0ul };
};

class Renderer
{
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual void on_window_resize() = 0;
    // virtual void set_screen(ScreenRect screen) = 0;
    virtual Handle<Buffer> make_buffer(const BufferDescriptor& info) = 0;
    virtual Handle<Image> make_image(const ImageDescriptor& info) = 0;
    virtual Handle<ImageView> make_view(const ImageViewDescriptor& info) = 0;
    virtual Handle<Sampler> make_sampler(const SamplerDescriptor& info) = 0;
    virtual Handle<Texture> make_texture(const TextureDescriptor& info) = 0;
    virtual Handle<Material> make_material(const MaterialDescriptor& info) = 0;
    virtual Handle<Geometry> make_geometry(const GeometryDescriptor& info) = 0;
    virtual Handle<Mesh> make_mesh(const MeshDescriptor& info) = 0;

    virtual Image& get_image(Handle<Image> image) = 0;

    virtual Handle<Mesh> instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(ecs::entity entity) = 0;
};

} // namespace gfx
} // namespace eng

// DEFINE_STD_HASH(gfx::ImageViewDescriptor, eng::hash::combine_fnv1a(t.view_type, t.format, t.aspect, t.layers, t.mips));
DEFINE_STD_HASH(eng::gfx::Geometry, eng::hash::combine_fnv1a(t.vertex_range, t.index_range, t.meshlet_range));
DEFINE_STD_HASH(eng::gfx::Material, eng::hash::combine_fnv1a(t.mesh_pass, t.base_color_texture));
DEFINE_STD_HASH(eng::gfx::Mesh, eng::hash::combine_fnv1a(t.geometry, t.material));
DEFINE_STD_HASH(eng::gfx::MeshPass, eng::hash::combine_fnv1a(t.name));
DEFINE_STD_HASH(eng::gfx::ShaderEffect, eng::hash::combine_fnv1a(t.pipeline));
DEFINE_STD_HASH(eng::gfx::SamplerDescriptor,
                eng::hash::combine_fnv1a(t.filtering[0], t.filtering[1], t.addressing[0], t.addressing[1], t.addressing[2],
                                         t.mip_lod[0], t.mip_lod[1], t.mip_lod[2], t.mipmap_mode, t.reduction_mode));
DEFINE_STD_HASH(eng::gfx::Sampler, eng::hash::combine_fnv1a(t.info));
DEFINE_STD_HASH(eng::gfx::Texture, eng::hash::combine_fnv1a(t.view, t.layout, t.sampler));
DEFINE_STD_HASH(eng::gfx::ImageView, eng::hash::combine_fnv1a(t.image, t.type, t.format, t.aspect, t.mips, t.layers));

ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Buffer);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Image);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::ImageView);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Sampler);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Texture);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Material);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Geometry);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Mesh);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::Pipeline);
ENG_DEFINE_HANDLE_DISPATCHER(eng::gfx::MeshPass);