#pragma once
#include <glm/glm.hpp>
#include "handle.hpp"
#include "common/spatial.hpp"

struct ModelAsset;
struct MeshAsset;
struct RenderInstance;
struct RenderMesh;
struct RenderBLAS;

namespace cmps {

struct Transform {
    glm::mat4 transform{ 1.0f };
};

struct RenderMesh {
    ModelAsset* asset{};
    MeshAsset* mesh{};
    Handle<RenderInstance> ri_handle{};
    Handle<::RenderMesh> rm_handle{};
    Handle<RenderBLAS> blas_handle{};
};

} // namespace cmps