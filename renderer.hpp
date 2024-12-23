#pragma once

#include <glm/mat4x3.hpp>
#include "model_importer.hpp"
#include "handle.hpp"
#include "common/flags.hpp"
#include "common/types.hpp"

enum class BatchFlags {};
enum class InstanceFlags { RAY_TRACED_BIT = 0x1 };
enum class ImageFormat {
    UNORM,
    SRGB,
};

struct RenderGeometry;
struct RenderMesh;
struct RenderInstance;
struct RenderBLAS;
struct RenderBLAS;
struct Image;
struct RenderMaterial;

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
    std::span<const std::byte> data;
};

struct MaterialDescriptor {
    Handle<Image> base_color_texture;
    Handle<Image> normal_texture;
    Handle<Image> metallic_roughness_texture;
};

struct MeshDescriptor {
    Handle<RenderGeometry> geometry{};
};

struct InstanceSettings {
    Flags<InstanceFlags> flags;
    Handle<Entity> entity;
    Handle<RenderMesh> mesh;
    Handle<RenderMaterial> material;
    glm::mat4 transform{ 1.0f };
};

struct BLASInstanceSettings {
    Handle<Entity> entity;
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
    virtual Handle<RenderInstance> instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(Handle<RenderInstance> handle) = 0;
};
