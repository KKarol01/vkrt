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
#include <iostream>
#include "vulkan_structs.hpp"
#include "model_importer.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "handle_vec.hpp"
#include "ecs.hpp"

int main() {
    Engine::init();

    {
        //Handle<ModelAsset> sphere_handle = Engine::scene()->load_from_file("sphere/sphere.glb");
        Handle<ModelAsset> cornell_handle = Engine::scene()->load_from_file("pbr_sphere/pbr_sphere.glb");
        /*Engine::scene()->instance_model(sphere_handle, { .flags = {},
                                                          .transform = glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.5f }) });*/
        Engine::scene()->instance_model(cornell_handle, { .flags = InstanceFlags::RAY_TRACED_BIT,
                                                          .transform = glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 1.0f }) });
    }

    Engine::start();
}