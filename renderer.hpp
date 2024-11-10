#pragma once

#include <glm/mat4x3.hpp>
#include "model_importer.hpp"
#include "handle.hpp"
#include "common/flags.hpp"
#include "common/types.hpp"

enum class BatchFlags {};
enum class InstanceFlags { RAY_TRACED_BIT = 0x1 };

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
    std::span<const std::byte> data;
};

struct MaterialDescriptor {
    Handle<Image> color_texture;
};

struct MeshDescriptor {
    Handle<RenderGeometry> geometry;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t vertex_count;
    uint32_t index_count;
};

struct InstanceSettings {
    Flags<InstanceFlags> flags;
    Handle<Entity> entity;
    Handle<RenderMaterial> material;
    Handle<RenderMesh> mesh;
    glm::mat4 transform{ 1.0f };
};

struct BLASInstanceSettings {
    Handle<Entity> entity;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual Handle<Image> batch_texture(const ImageDescriptor& batch) = 0;
    virtual Handle<RenderMaterial> batch_material(const MaterialDescriptor& batch) = 0;
    virtual Handle<RenderGeometry> batch_geometry(const GeometryDescriptor& batch) = 0;
    virtual Handle<RenderMesh> batch_mesh(const MeshDescriptor& batch) = 0;
    virtual void instance_mesh(const InstanceSettings& settings) = 0;
    virtual void instance_blas(const BLASInstanceSettings& settings) = 0;
    virtual void update_transform(Handle<RenderInstance> handle) = 0;
};