#include "app.hpp"

#include <eng/renderer/renderer.hpp>
#include <eng/engine.hpp>
#include <eng/scene.hpp>
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
        // Engine::get().ecs->traverse_hierarchy(e, [](auto p, auto e) {
        //     if(Engine::get().ecs->has<ecs::Mesh>(e))
        //     {
        //         Engine::get().renderer->submit_mesh(gfx::SubmitInfo{ e, gfx::MeshPassType::FORWARD });
        //     };
        // });
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
    auto* ecs = Engine::get().ecs;

    // auto e1 = ecs->create();
    // ecs->emplace(e1, ecs::Node{}, ecs::Transform{});
    // auto v1 =
    //     Engine::get().ecs->get_view<ecs::Mesh, ecs::Transform>([](auto e) { ENG_LOG("NEW ENTITY IN VIEW 1 {}", e); },
    //                                                            [](auto e, auto sig) {
    //                                                                ENG_LOG("ENTS UPDATED 1 {} {}", e, sig.to_string());
    //                                                            });
    // auto v2 = Engine::get().ecs->get_view<ecs::Transform>([](auto e) { ENG_LOG("NEW ENTITY IN VIEW 2 {}", e); },
    //                                                       [](auto e, auto sig) {
    //                                                           ENG_LOG("ENTS UPDATED 2 {} {}", e, sig.to_string());
    //                                                       });
    // for(auto [e, m, t] : v1)
    //{
    //     ENG_LOG("ENTS IN VIEW 1 {}", e);
    // }
    // for(auto [e, t] : v2)
    //{
    //     ENG_LOG("ENTS IN VIEW 2 {}", e);
    // }
    // auto e2 = ecs->create();
    // struct XX
    //{
    // };
    // ecs->emplace(e2, ecs::Transform{}, ecs::Mesh{});
    // ecs->emplace(e2, XX{});

    // for(auto [e, m, t] : v1)
    //{
    //     ENG_LOG("ENTS IN VIEW 1 {}", e);
    // }
    // for(auto [e, t] : v2)
    //{
    //     ENG_LOG("ENTS IN VIEW 2 {}", e);
    // }

    // auto* e1t = ecs->get<ecs::Transform>(e1);
    // auto* e2m = ecs->get<ecs::Mesh>(e2);

    // e2m->name = "sdafsdf";
    // ecs->update<ecs::Mesh>(e2);
    // ecs->update<ecs::Transform>(e1);

    renderer.init();
    const auto e = Engine::get().scene->load_from_file("cyberpunk.glb");

    glm::vec3 aabbMin(-10.0f, -5.0f, -5.0f);
    glm::vec3 aabbMax(10.0f, 5.0f, 5.0f);
    glm::uvec3 resolution(10, 5, 10); // lights per axis

    uint32_t numLights = resolution.x * resolution.y * resolution.z;
    glm::vec3 step = (aabbMax - aabbMin) / glm::vec3(resolution - 1u);

    //{
    //    SparseSet s;
    //    auto it = s.erase(2);
    //    it = s.insert(1);
    //    it = s.erase(2);
    //    it = s.erase(1);
    //    it = s.insert(2);
    //    it = s.insert(3);
    //    it = s.insert(1);
    //    it = s.insert(2);
    //    it = s.insert(3);
    //    it = s.insert(1);
    //    it = s.insert(2);
    //    it = s.insert(3);
    //    it = s.erase(1);
    //    it = s.insert();
    //    auto x = s.get(it);
    //}

    for(uint32_t z = 0; z < resolution.z; ++z)
    {
        for(uint32_t y = 0; y < resolution.y; ++y)
        {
            for(uint32_t x = 0; x < resolution.x; ++x)
            {
                uint32_t i = x + y * resolution.x + z * resolution.x * resolution.y;
                glm::vec3 pos = aabbMin + glm::vec3(x, y, z) * step;

                auto light = ecs->create();
                ecs->emplace(light, ecs::Node{ .name = ENG_FMT("LIGHT {}", i) }, ecs::Transform::from(pos),
                             ecs::Light{ .range = 2.0f, .type = ecs::Light::Type::POINT });
                Engine::get().scene->scene.push_back(light);
                // Engine::get().renderer->add_light(light);
            }
        }
    }

    Engine::get().scene->instance_entity(e);
    // Engine::get().scene->instance_entity(e);
}

void App::on_update() { renderer.update(); }

} // namespace app
