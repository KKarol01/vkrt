#include <volk/volk.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include "vulkan_structs.hpp"
#include "model_importer.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"

struct Camera {
    glm::mat4 get_view() const { return glm::lookAt(position, position + direction, glm::vec3{ 0.0f, 1.0f, 0.0f }); }

    glm::vec3 position{ 0.0f, 0.0f, 2.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

static Camera camera;

int main() {
    try {
        Engine::init();

        HandleBatchedModel cornell, sphere;
        {
            ImportedModel import_model = ModelImporter::import_model("cornell_box", "cornell/cornell.glb");
            ImportedModel import_sphere = ModelImporter::import_model("sphere", "sphere/sphere.glb");
            sphere = Engine::renderer()->batch_model(import_sphere, { .flags = BatchFlags::RAY_TRACED_BIT });
            cornell = Engine::renderer()->batch_model(import_model, { .flags = BatchFlags::RAY_TRACED_BIT });

            Engine::renderer()->instance_model(cornell, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED_BIT });
        }

        const auto* window = Engine::window();
        auto* vk_renderer = static_cast<RendererVulkan*>(Engine::renderer());

        const glm::mat4 projection = glm::perspectiveFov(glm::radians(90.0f), (float)window->size[0], (float)window->size[1], 0.01f, 1000.0f);
        const glm::mat4 view = camera.get_view();
        const glm::mat4 inv_projection = glm::inverse(projection);
        const glm::mat4 inv_view = glm::inverse(view);
        memcpy(vk_renderer->ubo.mapped, &inv_view[0][0], sizeof(inv_view));
        memcpy(static_cast<std::byte*>(vk_renderer->ubo.mapped) + sizeof(inv_view), &inv_projection[0][0], sizeof(inv_projection));

        while(!glfwWindowShouldClose(Engine::window()->window)) {
            vk_renderer->render();
            glfwPollEvents();
        }
    } catch(const std::exception& err) {
        std::cerr << err.what();
        return -1;
    }
}