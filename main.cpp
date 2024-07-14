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

struct BoundingBox {
    glm::vec3 center() const { return (max + min) * 0.5f; }
    glm::vec3 size() const { return (max - min); }
    glm::vec3 extent() const { return glm::abs(size() * 0.5f); }

    glm::vec3 min{ FLT_MAX }, max{ -FLT_MAX };
};

int main() {
    try {
        Engine::init();

        HandleBatchedModel cornell, sphere;
        BoundingBox box;
        {
            ImportedModel import_model = ModelImporter::import_model("cornell_box", "cornell/cornell.glb");
            ImportedModel import_sphere = ModelImporter::import_model("sphere", "sphere/sphere.glb");
            sphere = Engine::renderer()->batch_model(import_sphere, { .flags = BatchFlags::RAY_TRACED_BIT });
            cornell = Engine::renderer()->batch_model(import_model, { .flags = BatchFlags::RAY_TRACED_BIT });

            Engine::renderer()->instance_model(cornell, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED_BIT });

            for(const auto& v : import_model.vertices) {
                box.min = glm::min(box.min, v.pos);
                box.max = glm::max(box.max, v.pos);
            }
        }

        box.min *= glm::vec3{ 0.9f, 0.7, 0.9f };
        box.max *= glm::vec3{ 0.9f, 0.7, 0.9f };

        const float probe_distance = 0.5f;

        glm::uvec3 probe_counts{ box.size() / probe_distance };
        probe_counts.x = std::bit_ceil(probe_counts.x);
        probe_counts.y = std::bit_ceil(probe_counts.y);
        probe_counts.z = std::bit_ceil(probe_counts.z);

        const glm::vec3 probe_walk = box.size() / glm::vec3{ glm::max(probe_counts - 1u, glm::uvec3{ 1 }) };
        for(int i = 0; i < probe_counts.x; ++i) {
            for(int j = 0; j < probe_counts.y; ++j) {
                for(int k = 0; k < probe_counts.z; ++k) {
                    const glm::vec3 pos = box.min + probe_walk * glm::vec3{ i, j, k };

                    Engine::renderer()->instance_model(sphere, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED_BIT,
                                                                                 .transform = glm::translate(glm::mat4{ 1.0f }, pos) *
                                                                                              glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.1f }) });
                }
            }
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