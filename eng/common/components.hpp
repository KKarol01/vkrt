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

/* For rendering */
struct Renderable {
    Flags<gfx::InstanceFlags> flags;
    Handle<gfx::Mesh> mesh_handle;
    Handle<gfx::Material> material_handle;
     Handle<gfx::BLAS> blas_handle;
};

} // namespace components