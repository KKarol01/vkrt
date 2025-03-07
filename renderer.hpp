#pragma once

#include <span>
#include <glm/mat4x3.hpp>
#include "model_importer.hpp"
#include "handle.hpp"
#include "common/flags.hpp"
#include "common/types.hpp"
#include "ecs.hpp"

enum class BatchFlags {};
enum class InstanceFlags { RAY_TRACED_BIT = 0x1 };
enum class ImageFormat {
    UNORM,
    SRGB,
};
enum class ImageType { DIM_INVALID, DIM_1D, DIM_2D, DIM_3D };
enum class ImageFilter { LINEAR, NEAREST };
enum class ImageAddressing { REPEAT, CLAMP };

struct RenderGeometry;
struct RenderMesh;
struct RenderInstance;
struct Image;

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
    ImageFormat format{ ImageFormat::UNORM };
    ImageType type{ ImageType::DIM_2D };
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

struct RenderMaterial {
    MaterialDescriptor textures{};
};

struct MeshDescriptor {
    Handle<RenderGeometry> geometry{};
};

struct InstanceSettings {
    // Primitive's entity (scene::NodeInstance's primitives)
    components::Entity entity;
};

struct BLASInstanceSettings {
    components::Entity entity;
};

struct ScreenRect {
    float x;
    float y;
    float w;
    float h;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual void on_window_resize() = 0;
    virtual void set_screen(ScreenRect screen) = 0;
    virtual Handle<Image> batch_texture(const ImageDescriptor& batch) = 0;
    virtual Handle<RenderMaterial> batch_material(const MaterialDescriptor& batch) = 0;
    virtual Handle<RenderGeometry> batch_geometry(const GeometryDescriptor& batch) = 0;
    virtual Handle<RenderMesh> batch_mesh(const MeshDescriptor& batch) = 0;
    virtual void instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(components::Entity entity) = 0;
    virtual size_t get_imgui_texture_id(Handle<Image> handle, ImageFilter filter, ImageAddressing addressing) = 0;
    virtual RenderMaterial get_material(Handle<RenderMaterial> handle) const = 0;
};
