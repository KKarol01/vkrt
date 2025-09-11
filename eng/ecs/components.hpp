#pragma once
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/spatial.hpp>
#include <eng/common/flags.hpp>

namespace eng
{
namespace gfx
{
struct Mesh;
struct Geometry;
struct Material;
} // namespace gfx
} // namespace eng

namespace eng
{
namespace ecs
{
struct Transform
{
    glm::mat4 local{ 1.0f };
    glm::mat4 global{ 1.0f };
};

struct Node
{
    std::string name;
};

struct Mesh
{
    std::string name;
    std::vector<Handle<gfx::Mesh>> meshes;
    uint32_t gpu_resource{ ~0u };
};

} // namespace ecs
} // namespace eng
