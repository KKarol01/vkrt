#pragma once
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/spatial.hpp>
#include <eng/common/flags.hpp>

namespace gfx
{
struct Mesh;
struct Geometry;
struct Material;
} // namespace gfx

namespace ecs
{
namespace comp
{
struct Node
{
    std::string name;
    std::vector<Handle<gfx::Mesh>> meshes;
};

struct Mesh
{
    Handle<gfx::Geometry> geometry;
    Handle<gfx::Material> material;
};

struct Transform
{
    glm::mat4 local{ 1.0f };
    glm::mat4 global{ 1.0f };
};

} // namespace comp
} // namespace ecs