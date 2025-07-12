#pragma once

#include <cstdint>
#include <span>
#include <compare>
#include <array>
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/common/hash.hpp>

namespace gfx
{

struct Buffer;
struct Image;
struct ImageLayout;
struct ImageView;
struct Sampler;
struct Pipeline;

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
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
};

enum class ImageType
{
    TYPE_1D,
    TYPE_2D,
    TYPE_3D,
};

enum class ImageViewType
{
    TYPE_1D,
    TYPE_2D,
    TYPE_3D,
};

enum class ImageFiltering
{
    LINEAR,
    NEAREST
};

enum class ImageAddressing
{
    REPEAT,
    CLAMP_EDGE
};

enum class PassType
{
    FORWARD,
    DIRECTIONAL_SHADOW,
    LAST_ENUM,
};

struct Geometry
{
    auto operator<=>(const Geometry& t) const = default;
    Range vertex_range{};  // position inside vertex buffer
    Range index_range{};   // position inside index buffer
    Range meshlet_range{}; // position inside meshlet buffer
    // VkAccelerationStructureKHR blas{};
    // Handle<Buffer> blas_buffer{};
};

struct Texture
{
    auto operator<=>(const Texture& t) const = default;
    Handle<Image> image;
    Handle<ImageView> view;
    Handle<ImageLayout> layout;
    Handle<Sampler> sampler;
};

struct ShaderEffect {
    Handle<Pipeline> pipeline;
};

struct MeshPass
{
    std::array<Handle<ShaderEffect>, (size_t)PassType::LAST_ENUM> effects;
};

struct Material
{
    bool operator==(const Material& t) const
    {
        return base_color_texture == t.base_color_texture && mesh_pass == t.mesh_pass;
    }
    std::string mesh_pass{ "default_unlit" };
    Handle<Texture> base_color_texture{ ~0u };
};

// struct Submesh
//{
//     auto operator<=>(const Submesh& t) const = default;
// };

struct Mesh
{
    auto operator<=>(const Mesh& t) const = default;
    // std::string name;
    // Range submeshes{};
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
    auto operator==(const Meshlet&) const { return false; }
    uint32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t triangle_offset;
    uint32_t triangle_count;
    glm::vec4 bounding_sphere{};
};

struct GeometryDescriptor
{
    Flags<GeometryFlags> flags;
    std::span<const Vertex> vertices;
    std::span<uint32_t> indices;
};

struct ImageDescriptor
{
    auto operator==(const ImageDescriptor&) const { return false; }
    std::string name;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{}; // for 2d, depth should be 0, even though the apis require it to be 1 (for automatic type deduction)
    uint32_t mips{ 1 };
    ImageFormat format{ ImageFormat::R8G8B8A8_UNORM };
    ImageType type{ ImageType::TYPE_2D };
    std::span<const std::byte> data;
};

struct ImageViewDescriptor
{
    auto operator==(const ImageViewDescriptor& o) const
    {
        return view_type == o.view_type && format == o.format && mips == o.mips && layers == o.layers;
    }

    std::string name;
    std::optional<ImageViewType> view_type;
    std::optional<ImageFormat> format;
    Range mips{ 0, ~0u };
    Range layers{ 0, ~0u };
    // swizzle always identity for now
};

struct TextureDescriptor
{
    Handle<Image> image;
    ImageFiltering filtering{ ImageFiltering::LINEAR };
    ImageAddressing addressing{ ImageAddressing::REPEAT };
};

struct MaterialDescriptor
{
    std::string material_pass{ "default_unlit" };
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
    Handle<Mesh> mesh;
};

struct BLASInstanceSettings
{
    ecs::Entity entity;
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

class Renderer
{
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual void on_window_resize() = 0;
    // virtual void set_screen(ScreenRect screen) = 0;
    virtual Handle<Image> batch_image(const ImageDescriptor& batch) = 0;
    virtual Handle<Texture> batch_texture(const TextureDescriptor& batch) = 0;
    virtual Handle<Material> batch_material(const MaterialDescriptor& batch) = 0;
    virtual Handle<Geometry> batch_geometry(const GeometryDescriptor& batch) = 0;
    virtual Handle<Mesh> batch_mesh(const MeshDescriptor& batch) = 0;
    virtual Handle<Mesh> instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(ecs::Entity entity) = 0;
    virtual size_t get_imgui_texture_id(Handle<Image> handle, ImageFiltering filter, ImageAddressing addressing, uint32_t layer) = 0;
    virtual Handle<Image> get_color_output_texture() const = 0;
};

} // namespace gfx

DEFINE_STD_HASH(gfx::ImageViewDescriptor, eng::hash::combine_fnv1a(t.view_type, t.format, t.layers, t.mips));
DEFINE_STD_HASH(gfx::Geometry, eng::hash::combine_fnv1a(t.vertex_range, t.index_range, t.meshlet_range));
DEFINE_STD_HASH(gfx::Texture, eng::hash::combine_fnv1a(t.image, t.view, t.layout, t.sampler));
DEFINE_STD_HASH(gfx::Material, eng::hash::combine_fnv1a(t.base_color_texture, t.pipeline));
DEFINE_STD_HASH(gfx::Mesh, eng::hash::combine_fnv1a(t.geometry, t.material));