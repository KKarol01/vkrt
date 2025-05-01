#pragma once
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/spatial.hpp>
#include <eng/common/flags.hpp>

namespace gfx {
struct ModelAsset;
struct MeshAsset;
struct Mesh;
struct BLAS;
struct Geometry;
struct Material;
enum class InstanceFlags;
} // namespace gfx

namespace components {

/* Position on scene */
struct Transform {
    /* World position after traversing node hierarchy */
    glm::mat4 transform{ 1.0f };
};

struct Submesh {
    Handle<gfx::Geometry> geometry;
    Handle<gfx::Material> material;
    Handle<gfx::BLAS> blas;
};

struct Mesh {
    std::string name;
    std::vector<Submesh> submeshes;
};

} // namespace components