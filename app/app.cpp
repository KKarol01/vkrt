#include "app.hpp"

#include <eng/engine.hpp>
#include <eng/scene.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <assets/shaders/bindless_structures.glsli>
#include <eng/utils.hpp>

// todo: :(
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/common/to_vk.hpp>
#include <vulkan/vulkan.h>

using namespace eng;

namespace app
{

void Renderer::init() {}

void Renderer::update()
{
    for(auto e : Engine::get().scene->scene)
    {
        Engine::get().ecs->traverse_hierarchy(e, [](auto p, auto e) {
            if(Engine::get().ecs->has<ecs::Mesh>(e))
            {
                Engine::get().renderer->submit_mesh(gfx::SubmitInfo{ e, gfx::MeshPassType::FORWARD });
            };
        });
    }
}

void App::start()
{
    Engine::get().on_init += [this] { on_init(); };
    Engine::get().on_update += [this] { on_update(); };
}

void App::on_init()
{
    renderer.init();
    const auto e = Engine::get().scene->load_from_file("occlusion_culling.glb");
    Engine::get().scene->instance_entity(e);
}

void App::on_update() { renderer.update(); }

} // namespace app