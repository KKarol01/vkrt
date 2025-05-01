#pragma once

#include <span>
#include <glm/mat4x3.hpp>
#include <eng/model_importer.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/ecs.hpp>

namespace gfx {

enum class BatchFlags {};
enum class InstanceFlags { RAY_TRACED_BIT = 0x1 };
enum class ImageFormat {
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
};
enum class ImageType { TYPE_1D, TYPE_2D, TYPE_3D };
enum class ImageFilter { LINEAR, NEAREST };
enum class ImageAddressing { REPEAT, CLAMP_EDGE };

struct Geometry;
struct Mesh;
struct Image;
struct Buffer;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 nor;
    glm::vec2 uv;
    glm::vec4 tang;
};

struct GeometryDescriptor {
    std::span<const Vertex> vertices;
    std::span<const uint32_t> indices;
};

struct ImageDescriptor {
    std::string name;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1 };
    uint32_t mips{ 1 };
    ImageFormat format{ ImageFormat::R8G8B8A8_UNORM };
    ImageType type{ ImageType::TYPE_2D };
    std::span<const std::byte> data;
};

struct MaterialImageDescriptor {
    Handle<Image> handle;
    ImageFilter filter{ ImageFilter::LINEAR };
    ImageAddressing addressing{ ImageAddressing::REPEAT };
};

struct MaterialDescriptor {
    MaterialImageDescriptor base_color_texture;
    MaterialImageDescriptor normal_texture;
    MaterialImageDescriptor metallic_roughness_texture;
};

struct Material {
    MaterialDescriptor textures{};
};

struct MeshDescriptor {
    Handle<Geometry> geometry{};
};

struct InstanceSettings {
    // Primitive's entity (scene::NodeInstance's primitives)
    components::Entity entity;
};

struct BLASInstanceSettings {
    components::Entity entity;
};

// struct ScreenRect {
//     float x;
//     float y;
//     float w;
//     float h;
// };

struct VsmData {
    Handle<Buffer> constants_buffer;
    Handle<Buffer> free_allocs_buffer;
    Handle<Image> shadow_map_0;
    // VkImageView view_shadow_map_0_general{};
    Handle<Image> dir_light_page_table;
    // VkImageView view_dir_light_page_table_general{};
    Handle<Image> dir_light_page_table_rgb8;
    // VkImageView view_dir_light_page_table_rgb8_general{};
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual void on_window_resize() = 0;
    // virtual void set_screen(ScreenRect screen) = 0;
    virtual Handle<Image> batch_texture(const ImageDescriptor& batch) = 0;
    virtual Handle<Material> batch_material(const MaterialDescriptor& batch) = 0;
    virtual Handle<Geometry> batch_geometry(const GeometryDescriptor& batch) = 0;
    virtual Handle<Mesh> batch_mesh(const MeshDescriptor& batch) = 0;
    virtual void instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(components::Entity entity) = 0;
    virtual size_t get_imgui_texture_id(Handle<Image> handle, ImageFilter filter, ImageAddressing addressing, uint32_t layer) = 0;
    virtual Handle<Image> get_color_output_texture() const = 0;
    virtual Material get_material(Handle<Material> handle) const = 0;
    virtual VsmData& get_vsm_data() = 0;
};

} // namespace gfx