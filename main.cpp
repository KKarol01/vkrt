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

struct Camera {
    glm::mat4 get_view() const { return glm::lookAt(position, position + direction, glm::vec3{ 0.0f, 1.0f, 0.0f }); }

    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

static Camera camera;

// https://www.shadertoy.com/view/WlSSWc
float halton(int i, int b) {
    /* Creates a halton sequence of values between 0 and 1.
        https://en.wikipedia.org/wiki/Halton_sequence
        Used for jittering based on a constant set of 2D points. */
    float f = 1.0;
    float r = 0.0;
    while(i > 0) {
        f = f / float(b);
        r = r + f * float(i % b);
        i = i / b;
    }
    return r;
}

int main() {

    Engine::init();

    {
        ImportedModel import_cornell = ModelImporter::import_model("cornell_box", "cornell/cornell.glb");
        ImportedModel import_bunny = ModelImporter::import_model("bunny", "cornell/bunny.glb");
        ImportedModel import_gallery = ModelImporter::import_model("the picture gallery", "the_picture_gallery.glb");
        HandleBatchedModel gallery = Engine::renderer()->batch_model(import_gallery, { .flags = BatchFlags::RAY_TRACED });
        HandleBatchedModel cornell = Engine::renderer()->batch_model(import_cornell, { .flags = BatchFlags::RAY_TRACED });
        // HandleBatchedModel bunny = Engine::renderer()->batch_model(import_bunny, { .flags = BatchFlags::RAY_TRACED_BIT });

        Engine::renderer()->instance_model(gallery, InstanceSettings{
                                                        .flags = InstanceFlags::RAY_TRACED,
                                                        .transform = glm::rotate(glm::mat4{ 1.0f }, glm::radians(35.0f),
                                                                                 glm::vec3{ 0.0f, 1.0f, 0.0f }),
                                                    });
        Engine::renderer()->instance_model(cornell, InstanceSettings{
                                                        .flags = InstanceFlags::RAY_TRACED,
                                                        .transform = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 0.2, 0.0, -0.6 }) *
                                                                     glm::rotate(glm::mat4{ 1.0f }, -glm::radians(25.0f),
                                                                                 glm::vec3{ 0.0f, 1.0f, 0.0f }) *
                                                                     glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.09 }),
                                                    });
        //  Engine::renderer()->instance_model(bunny, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED_BIT });
    }

    const auto* window = Engine::window();
    auto* vk_renderer = static_cast<RendererVulkan*>(Engine::renderer());

    const glm::mat4 projection =
        glm::perspectiveFov(glm::radians(90.0f), (float)window->size[0], (float)window->size[1], 0.01f, 1000.0f);
    const glm::mat4 view = camera.get_view();
    const glm::mat4 inv_projection = glm::inverse(projection);
    const glm::mat4 inv_view = glm::inverse(view);
    vk_renderer->ubo.push_data(&inv_view, sizeof(inv_view), 0);
    vk_renderer->ubo.push_data(&inv_projection, sizeof(inv_projection), sizeof(inv_view));

    int num_frame = 0;

    FrameTime ft;
    double last_tick = glfwGetTime();

    while(!glfwWindowShouldClose(Engine::window()->window)) {
        if(glfwGetTime() - last_tick >= 1.0 / 60.0) {
            last_tick = glfwGetTime();
            float hx = halton(num_frame, 2) * 2.0 - 1.0;
            float hy = halton(num_frame, 3) * 2.0 - 1.0;
            num_frame = (num_frame + 1) % 4;
            glm::mat3 rand_mat =
                glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

            vk_renderer->ubo.push_data(&rand_mat, sizeof(rand_mat), sizeof(inv_view) + sizeof(inv_projection));

            vk_renderer->render();
            ft.update();
            // std::println("avg frame time: {}", ft.get_avg_frame_time());
        }

        glfwPollEvents();
    }
}