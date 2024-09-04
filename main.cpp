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
#include "handle_vector.hpp"

int main() {
    Engine::init();

    {
        ImportedModel import_cornell = ModelImporter::import_model("cornell_box", "cornell/cornell2.glb");
        // ImportedModel import_bunny = ModelImporter::import_model("bunny", "cornell/bunny.glb");
        ImportedModel import_gallery = ModelImporter::import_model("the picture gallery", "the_picture_gallery.glb");

        HandleBatchedModel gallery = Engine::renderer()->batch_model(import_gallery, { .flags = BatchFlags::RAY_TRACED });
        // HandleBatchedModel cornell = Engine::renderer()->batch_model(import_cornell, { .flags = BatchFlags::RAY_TRACED });

        /*HandleInstancedModel cornell_i1 =
            Engine::renderer()->instance_model(cornell, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED });*/
        Engine::renderer()->instance_model(gallery, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED });

        Engine::set_on_update_callback([&] {
            const glm::mat4x3 T = glm::rotate(glm::mat4{ 1.0f }, (float)Engine::get_time_secs(), glm::vec3{ 0.0f, 1.0f, 0.0f });
            //((RendererVulkan*)Engine::renderer())->update_transform(cornell_i1, T);
        });
    }

    Engine::start();
}