#include <volk/volk.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <GLFW/glfw3native.h>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "./engine.hpp"

int main()
{
    Engine::get().init();

    const auto scene_bunny = Engine::get().scene->load_from_file("bunny.glb");
    const auto bunny_instance = Engine::get().scene->instance_model(scene_bunny);

    Engine::get().start();
}
