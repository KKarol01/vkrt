#pragma once
#include <glm/glm.hpp>
#include "handle.hpp"
#include "common/spatial.hpp"
#include "common/flags.hpp"

struct ModelAsset;
struct MeshAsset;
struct RenderMesh;
struct RenderBLAS;
struct RenderGeometry;
struct RenderMaterial;
enum class InstanceFlags;

namespace components {

/* Position on scene */
struct Transform {
    /* World position after traversing node hierarchy */
    glm::mat4 transform{ 1.0f };
};

/* For rendering */
struct Renderable {
    Flags<InstanceFlags> flags;
    Handle<RenderMesh> mesh_handle;
    Handle<RenderMaterial> material_handle;
    Handle<RenderBLAS> blas_handle;
};

} // namespace components