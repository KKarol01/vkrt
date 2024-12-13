#pragma once
#include <glm/glm.hpp>
#include "handle.hpp"
#include "common/spatial.hpp"

struct ModelAsset;
struct MeshAsset;
struct RenderInstance;
struct RenderMesh;
struct RenderBLAS;
struct RenderGeometry;
struct RenderMaterial;

namespace cmps {

struct Transform {
    glm::mat4 transform{ 1.0f };
};



} // namespace cmps