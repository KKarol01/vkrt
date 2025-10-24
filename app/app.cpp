#include "app.hpp"


#include <eng/engine.hpp>
#include <eng/scene.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <assets/shaders/bindless_structures.glsli>
#include <eng/common/handle.hpp>
#include <eng/renderer/submit_queue.hpp>
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

    // Engine::get().renderer->rgraph->add_pass(
    //     gfx::RenderGraph::PassCreateInfo{ "ex1", gfx::RenderOrder::DEFAULT_UNLIT - 1 },
    //     [](gfx::RenderGraph::PassResourceBuilder& b) {
    //         b.access(Engine::get().renderer->render_passes.at((uint32_t)gfx::MeshPassType::FORWARD).cmd_buf,
    //                  gfx::RenderGraph::AccessType::RW, gfx::PipelineStage::COMPUTE_BIT, gfx::PipelineAccess::SHADER_RW);
    //     },
    //     [](gfx::SubmitQueue* q, gfx::CommandBuffer* cmd) {

    //    });
}

void App::start()
{
    Engine::get().on_init += [this] { on_init(); };
    Engine::get().on_update += [this] { on_update(); };
}

void App::on_init()
{
    renderer.init();
    const auto e = Engine::get().scene->load_from_file("container.glb");

    auto* ecs = Engine::get().ecs;
    auto light = ecs->create();
    ecs->emplace<ecs::Node>(light, ecs::Node{ .name = "light" });
    ecs->emplace<ecs::Transform>(light, ecs::Transform::from({ 0.0f, 0.0f, 1.0f }));
    ecs->emplace<ecs::Light>(light, ecs::Light{ .range = 5.0f, .type = ecs::Light::Type::POINT });

    Engine::get().scene->instance_entity(e);
    Engine::get().scene->scene.push_back(light);
    Engine::get().renderer->add_light(light); // todo: this should be automated; maybe stage - same as meshes
                                              // also, static/dynamic should be added
}

void App::on_update() { renderer.update(); }

} // namespace app