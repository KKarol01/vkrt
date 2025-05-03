#include "./engine.hpp"
#include <eng/assets/importer.hpp>

int main() {
    Engine::get().init();
    {
        Handle<scene::Node> model = Engine::get().scene->load_from_file("boxplane.glb");
        // Engine::get().set_on_update_callback([&]() {
        //     auto& ni = Engine::get().scene->get_instance(cornell_instance);
        //     // Engine::get().scene->update_transform(cornell_instance, ni.transform * glm::rotate(glm::radians(1.0f), glm::vec3{ 0.0f, 1.0f, 0.0f }));
        // });
    }
    Engine::get().start();
}
