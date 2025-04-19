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

int main() {
    Engine::get().init();
    {
        Handle<scene::Node> model = Engine::get().scene->load_from_file("boxplane_cars.glb");
        // Handle<Entity> model = Engine::scene()->load_from_file("bistro.glb");
        /* Engine::scene()->instance_model(sphere_handle, { .flags = {}, .transform = glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.5f }) }); */
        auto cornell_instance = Engine::get().scene->instance_model(model);
        // Engine::get().scene->update_transform(cornell_instance, glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 2.0f }));

        Engine::get().set_on_update_callback([&]() {
            auto& ni = Engine::get().scene->get_instance(cornell_instance);
            //Engine::get().scene->update_transform(cornell_instance, ni.transform * glm::rotate(glm::radians(1.0f), glm::vec3{ 0.0f, 1.0f, 0.0f }));
        });
    }
    Engine::get().start();
}
