#pragma once

#include <span>
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/ecs/ecs.hpp>

namespace gfx
{

enum class GeometryFlags
{
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
    TYPE_3D
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

struct Geometry;
struct Mesh;
struct Image;
struct Texture;
struct Buffer;
struct Material;

using Index = uint32_t;

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Meshlet
{
    Range vertex_range;
    Range triangle_range;
    glm::vec4 bounding_sphere{};
};

struct GeometryDescriptor
{
    Flags<GeometryFlags> flags;
    std::span<const Vertex> vertices;
    std::span<const Index> indices;
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
    std::span<const std::byte> data;
};

struct TextureDescriptor
{
    Handle<Image> image;
    ImageFiltering filtering{ ImageFiltering::LINEAR };
    ImageAddressing addressing{ ImageAddressing::REPEAT };
};

struct MaterialDescriptor
{
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
    // Primitive's entity (scene::NodeInstance's primitives)
    ecs::Entity entity;
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
    virtual void instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(ecs::Entity entity) = 0;
    virtual size_t get_imgui_texture_id(Handle<Image> handle, ImageFiltering filter, ImageAddressing addressing, uint32_t layer) = 0;
    virtual Handle<Image> get_color_output_texture() const = 0;
    virtual Material get_material(Handle<Material> handle) const = 0;
};

} // namespace gfx