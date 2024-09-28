#pragma once

#include <glm/mat4x3.hpp>
#include "model_importer.hpp"
#include "handle.hpp"
#include "flags.hpp"

enum class BatchFlags {};
enum class InstanceFlags { RAY_TRACED_BIT = 0x1 };

struct GeometryBatch;
struct TextureBatch;
struct MeshBatch;
struct MeshInstance;
struct BLASBatch;
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

struct TextureBatch {
    std::string name;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1 };
    uint32_t mips{ 1 };
    std::span<const std::byte> data;
};

struct MaterialBatch {
    Handle<TextureBatch> color_texture;
};

struct MeshDescriptor {
    Handle<GeometryBatch> geometry;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t vertex_count;
    uint32_t index_count;
};

struct InstanceSettings {
    Flags<InstanceFlags> flags;
    Handle<MeshBatch> mesh;
    Handle<MaterialBatch> material;
};

struct BLASInstanceSettings {
    Handle<GeometryBatch> geometry;
    std::vector<Handle<MeshInstance>> mesh_instances;
};

struct ScreenRect {
    int offset_x, offset_y;
    uint32_t width, height;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void render() = 0;
    virtual void set_screen_rect(ScreenRect rect) = 0;
    virtual Handle<TextureBatch> batch_texture(const TextureBatch& batch) = 0;
    virtual Handle<MaterialBatch> batch_material(const MaterialBatch& batch) = 0;
    virtual Handle<GeometryBatch> batch_geometry(const GeometryDescriptor& batch) = 0;
    virtual Handle<MeshBatch> batch_mesh(const MeshDescriptor& batch) = 0;
    virtual Handle<MeshInstance> instance_mesh(const InstanceSettings& settings) = 0;
    virtual Handle<BLASInstance> instance_blas(const BLASInstanceSettings& settings) = 0;
};
