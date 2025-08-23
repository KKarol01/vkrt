#include "app.hpp"

#include <eng/engine.hpp>
#include <eng/scene.hpp>

using namespace eng;

void App::start()
{
    Engine::get().on_init += [this] {
        const auto scene_bunny = Engine::get().scene->load_from_file("occlusion_culling1.glb");
        const auto scene_boxplane = Engine::get().scene->load_from_file("boxplane.glb");
        const auto bunny_instance = Engine::get().scene->instance_entity(scene_bunny);
        //const auto bunny_instance2 = Engine::get().scene->instance_entity(scene_boxplane);
    };
}