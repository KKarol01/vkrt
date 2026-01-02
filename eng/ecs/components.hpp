#pragma once
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/spatial.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/logger.hpp>
#include <assets/shaders/bindless_structures.glsli>

namespace eng
{
namespace gfx
{
struct Mesh;
struct Geometry;
struct Material;
} // namespace gfx

namespace asset
{
struct Model;
struct Mesh;
} // namespace asset

} // namespace eng

namespace eng
{
namespace ecs
{
struct Transform
{
    static Transform from(const glm::vec3& pos)
    {
        Transform t;
        t.local = glm::identity<glm::mat4>();
        t.global = glm::translate(pos);
        return t;
    }

    glm::vec3 pos() const { return glm::vec3{ global[3] }; }

    glm::mat4 local{ 1.0f };
    glm::mat4 global{ 1.0f };
};

struct Node
{
    std::string name;
    const asset::Model* model{};
};

struct Mesh
{
    const asset::Mesh* mesh{};
    std::vector<Handle<gfx::Mesh>> meshes;
    uint32_t gpu_resource{ ~0u };
};

struct alignas(16) Light
{
    enum class Type : uint32_t
    {
        POINT = GPU_LIGHT_TYPE_POINT,
    };

    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float range{};
    float intensity{ 1.0f };
    Type type;
    uint32_t gpu_index{ ~0u };
};

} // namespace ecs

// clang-format off
inline std::string to_string(const ecs::Light::Type& a) 
{
    switch(a) 
    {
        case ecs::Light::Type::POINT: { return "point"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}
// clang-format on

} // namespace eng
