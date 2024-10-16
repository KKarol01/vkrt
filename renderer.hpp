#pragma once

#include <glm/mat4x3.hpp>
#include "model_importer.hpp"
#include "handle.hpp"
#include "common/flags.hpp"
#include "common/types.hpp"

enum class BatchFlags {};
enum class InstanceFlags { RAY_TRACED_BIT = 0x1 };

struct RenderGeometry;
struct RenderTexture;
struct RenderMesh;
struct MeshInstance;
struct RenderBLAS;
struct BLASInstance;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 nor;
    glm::vec2 uv;
};

struct GeometryDescriptor {
    std::span<const Vertex> vertices;
    std::span<const uint32_t> indices;
};

struct RenderTexture {
    std::string name;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1 };
    uint32_t mips{ 1 };
    std::span<const std::byte> data;
};

struct MaterialBatch {
    Handle<RenderTexture> color_texture;
};

struct MeshDescriptor {
    Handle<RenderGeometry> geometry;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t vertex_count;
    uint32_t index_count;
};

struct InstanceSettings {
    std::string name;
    Flags<InstanceFlags> flags;
    Handle<Entity> entity;
    Handle<RenderMesh> mesh;
    Handle<MaterialBatch> material;
    glm::mat4x3 transform{ 1.0f };
};

struct BLASInstanceSettings {
    Handle<MeshInstance> render_instance;
};

struct ScreenRect {
    int offset_x, offset_y;
    uint32_t width, height;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual void set_screen_rect(ScreenRect rect) = 0;
    virtual Handle<RenderTexture> batch_texture(const RenderTexture& batch) = 0;
    virtual Handle<MaterialBatch> batch_material(const MaterialBatch& batch) = 0;
    virtual Handle<RenderGeometry> batch_geometry(const GeometryDescriptor& batch) = 0;
    virtual Handle<RenderMesh> batch_mesh(const MeshDescriptor& batch) = 0;
    virtual Handle<MeshInstance> instance_mesh(const InstanceSettings& settings) = 0;
    virtual Handle<BLASInstance> instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(Handle<MeshInstance> handle) = 0;
};
