#pragma once
#include <string>
#include <glm/common.hpp>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/quaternion.hpp>
#include <glm/glm/gtx/matrix_decompose.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/spatial.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/logger.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <assets/shaders/bindless_structures.glsli>

namespace eng
{
namespace ecs
{
struct Transform
{
    static Transform init(const glm::mat4& mat)
    {
        Transform t;
        glm::vec3 skew;
        glm::vec4 perspective;
        if(!glm::decompose(mat, t.scale, t.rotation, t.position, skew, perspective)) { return {}; }
        return t;
    }
    glm::mat4 to_mat4() const { return glm::translate(glm::scale(glm::mat4_cast(rotation), scale), position); }
    glm::vec3 position{};
    glm::quat rotation{ glm::quat_identity<float, glm::packed_highp>() };
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
};

struct Material
{
    std::string name;
    Handle<gfx::Material> render_material;
};

struct Geometry
{
    Handle<gfx::Geometry> render_geometry;
};

struct Mesh
{
    std::string name;
    Handle<gfx::Mesh> render_mesh;
    uint32_t geom_mat{};          // index scene's geometry or materials vectors with this
    uint32_t gpu_resource{ ~0u }; // renderer sets this when it processes the mesh
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
