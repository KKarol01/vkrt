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
#include <random>
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
            cornell = Engine::renderer()->batch_model(import_model, { .flags = BatchFlags::RAY_TRACED_BIT });
            sphere = Engine::renderer()->batch_model(import_sphere, { .flags = BatchFlags::RAY_TRACED_BIT });

            // TODO: because of sorting, the isntanced model handle is invalid
            Engine::renderer()->instance_model(
                cornell, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED_BIT,
                                           .transform = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 0.0, 0.0, 0.0 }) *
                                                        glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 1.0f }) });
        }

        const auto* window = Engine::window();
        auto* vk_renderer = static_cast<RendererVulkan*>(Engine::renderer());

        const glm::mat4 projection =
            glm::perspectiveFov(glm::radians(90.0f), (float)window->size[0], (float)window->size[1], 0.01f, 1000.0f);
        const glm::mat4 view = camera.get_view();
        const glm::mat4 inv_projection = glm::inverse(projection);
        const glm::mat4 inv_view = glm::inverse(view);
        memcpy(vk_renderer->ubo.mapped, &inv_view[0][0], sizeof(inv_view));
        memcpy(static_cast<std::byte*>(vk_renderer->ubo.mapped) + sizeof(inv_view), &inv_projection[0][0], sizeof(inv_projection));

        std::mt19937 eng;
        std::uniform_real_distribution<float> dist{ 0.0f, 1.0f };

        while(!glfwWindowShouldClose(Engine::window()->window)) {
            glm::vec3 rand{ dist(eng) * 2.0f * glm::pi<float>(), dist(eng) * 2.0f * glm::pi<float>(), dist(eng) * 2.0f };
            const auto rand_mat = (glm::mat3_cast(glm::angleAxis(rand.x, glm::vec3{ 1.0f, 0.0f, 0.0f }) *
                                                                glm::angleAxis(rand.y, glm::vec3{ 0.0f, 1.0f, 0.0f })));
            memcpy((glm::mat4*)vk_renderer->ubo.mapped + 2, &rand_mat[0][0], sizeof(rand_mat));

            vk_renderer->render();

            glfwPollEvents();
        }
    } catch(const std::exception& err) {
        std::cerr << err.what();
        return -1;
    }
}