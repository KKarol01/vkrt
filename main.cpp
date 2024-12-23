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
#include "engine.hpp"

int main() {
    Engine::init();
    {
        // Handle<ModelAsset> model = Engine::scene()->load_from_file("cornell/cornell1.glb");
        Handle<Entity> model = Engine::scene()->load_from_file("cornell/cornell1.glb");
        /* Engine::scene()->instance_model(sphere_handle, { .flags = {}, .transform = glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.5f }) }); */
        Engine::scene()->instance_model(model);
    }
    Engine::start();
}
