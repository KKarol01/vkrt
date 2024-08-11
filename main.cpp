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
        // ImportedModel import_bunny = ModelImporter::import_model("bunny", "cornell/bunny.glb");
        ImportedModel import_gallery = ModelImporter::import_model("the picture gallery", "the_picture_gallery.glb");

        HandleBatchedModel gallery = Engine::renderer()->batch_model(import_gallery, { .flags = BatchFlags::RAY_TRACED });
        HandleBatchedModel cornell = Engine::renderer()->batch_model(import_cornell, { .flags = BatchFlags::RAY_TRACED });

        Engine::renderer()->instance_model(cornell, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED });
        Engine::renderer()->instance_model(cornell, InstanceSettings{ .flags = InstanceFlags::RAY_TRACED,
                                                                      .transform = glm::translate(glm::mat4{ 1.0f },
                                                                                                  glm::vec3{ 1.0f, 0.0f, 0.0f }) });
    }

    Engine::start();
}