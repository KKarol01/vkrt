#pragma once
#include "handle.hpp"
#include <glm/glm.hpp>

struct ModelAsset;
struct Mesh;
struct MeshInstance;
struct BLASInstance;

namespace cmps {

struct Transform {
    glm::mat4 transform{ 1.0f };
};

struct RenderMesh {
    ModelAsset* asset;
    Mesh* mesh;
    Handle<MeshInstance> render_handle;
    Handle<BLASInstance> blas_handle;
};

} // namespace cmps