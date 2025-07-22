#define GLM_ENABLE_EXPERIMENTAL
#include <GLFW/glfw3native.h>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "./engine.hpp"

static glm::mat4 infinitePerspectiveFovReverseZRH_ZO(float fov, float width, float height, float zNear) {
    const float h = 1.0f / glm::tan(0.5f * fov);
    const float w = h * height / width;
    glm::mat4 result = glm::zero<glm::mat4>();
    result[0][0] = w;
    result[1][1] = h;
    result[2][2] = 0.0f;
    result[2][3] = -1.0f;
    result[3][2] = zNear;
    return result;
};

static bool project_sphere_bounds(glm::vec3 c, float r, float znear, float P00, float P11, glm::vec4& aabb)
{
    if (c.z < r + znear) return false;

    glm::vec3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    aabb = glm::vec4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    // clip space -> uv space
    aabb = glm::vec4{aabb.x,aabb.w,aabb.z,aabb.y,} * glm::vec4(0.5f, -0.5f, 0.5f, -0.5f) + glm::vec4(0.5f);
    return true;
}

int main()
{
    Engine::get().init();

    glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    float fov = glm::radians(60.0f);
    float aspect = 800.0f / 600.0f;
    float zNear = 0.1f;
    glm::mat4 proj = infinitePerspectiveFovReverseZRH_ZO(fov, 1280.0f, 768.0f, zNear);

    glm::vec4 p_world(0.0f, 0.0f, -13.0f, 1.0f);

    glm::vec4 p_eye = view * p_world;
    glm::vec4 p_clip = proj * p_eye;
    glm::vec3 p_ndc = glm::vec3(p_clip) / p_clip.w;

    glm::vec4 aabb;
    const auto res = project_sphere_bounds(glm::vec3{p_eye} * glm::vec3{1.0f, 1.0f, -1.0f}, 1.0f, 0.1f, proj[0][0], proj[1][1], aabb);
    const auto aabbc = (glm::vec2{ aabb.x, aabb.y } + glm::vec2{ aabb.z, aabb.w }) * 0.5f;
    const auto aabbc_projz = zNear / (-p_eye.z - 1.0f);
    const auto zbuffer = p_ndc.z;
    const auto comp = aabbc_projz >= zbuffer;

    const auto scene_bunny = Engine::get().scene->load_from_file("occlusion_culling1.glb");
    const auto scene_boxplane = Engine::get().scene->load_from_file("boxplane.glb");
    const auto bunny_instance = Engine::get().scene->instance_entity(scene_bunny);
    // const auto bunny_instance2 = Engine::get().scene->instance_entity(scene_boxplane);

    Engine::get().start();
}
