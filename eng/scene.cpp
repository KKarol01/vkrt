#include <stack>
#include <ranges>
#include <array>
#include <filesystem>
#include <unordered_map>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <eng/scene.hpp>
#include <eng/engine.hpp>
#include <eng/ecs/components.hpp>
#include <eng/common/logger.hpp>
#include <eng/fs/fs.hpp>
#include <eng/physics/bvh.hpp>
#include <eng/assets/asset_manager.hpp>

namespace eng
{

ecs::EntityId Scene::instance_asset(const assets::Asset& asset)
{
    ENG_ASSERT(asset.root_nodes.size() > 0);

    const auto recursive_instance = [&asset](const auto& self, const assets::Asset::Node& node,
                                             ecs::EntityId parent_entity) -> ecs::EntityId {
        auto* ecs = get_engine().ecs;
        auto e = ecs->create();
        ecs->add_components<ecs::Node>(e, ecs::Node{ .name = node.name });
        if(node.transform != ~0u)
        {
            ecs->add_components<ecs::Transform>(e, ecs::Transform{ asset.transforms[node.transform] });
        }
        if(node.meshes.size > 0)
        {
            ecs::Mesh ecsmesh{};
            ecsmesh.name = "EMPTY NAME";
            ecsmesh.render_meshes = { asset.meshes.begin() + node.meshes.offset,
                                      asset.meshes.begin() + node.meshes.offset + node.meshes.size };
            ecs->add_components<ecs::Mesh>(e, std::move(ecsmesh));
        }
        if(parent_entity) { ecs->make_child(parent_entity, e); }
        for(auto i = 0u; i < node.children.size; ++i)
        {
            self(self, asset.nodes[node.children.offset + i], e);
        }
        return e;
    };

    ecs::EntityId e;
    if(asset.root_nodes.size() > 1)
    {
        e = get_engine().ecs->create();
        get_engine().ecs->add_components<ecs::Node>(e, ecs::Node{ .name = ENG_FMT("{}_root", asset.path.filename().string()) });
        for(auto rn : asset.root_nodes)
        {
            const auto& node = asset.nodes[rn];
            recursive_instance(recursive_instance, node, e);
        }
    }
    else if(asset.root_nodes.size() == 1)
    {
        e = recursive_instance(recursive_instance, asset.nodes[asset.root_nodes[0]], ecs::EntityId{});
    }

    return e;
}

} // namespace eng

// void Scene::ui_draw_manipulate()
//{
//     if(!ui.scene.sel_entity) { return; }
//
//     auto* ecs = get_engine().ecs;
//     auto& entity = ui.scene.sel_entity;
//     auto& ctransform = ecs->get<ecs::Transform>(entity);
//     auto& cnode = ecs->get<ecs::Node>(entity);
//     auto& cmesh = ecs->get<ecs::Mesh>(entity);
//
//     auto& io = ImGui::GetIO();
//     auto& style = ImGui::GetStyle();
//     ImGui::PushStyleColor(ImGuiCol_WindowBg, 0u); // don't set no background, make host dock push style with no bg, and somehow it works -
//                                                   // the content window actually does not have the background
//     ImGui::Begin("Manipulate", 0, ImGuiWindowFlags_NoDecoration);
//     ImGui::PopStyleColor(1);
//     ImGuizmo::SetDrawlist();
//
//     const auto view = get_engine().camera->get_view();
//     auto proj = get_engine().camera->get_projection(); // imguizmo hates inf_revz_zo perspective matrix that i use (div by 0 because no far plane)
//     proj = glm::perspectiveFov(glm::radians(75.0f), get_engine().window->width, get_engine().window->height, 0.1f, 30.0f);
//     const auto window_width = ImGui::GetWindowWidth();
//     const auto window_height = ImGui::GetWindowHeight();
//     const auto window_pos = ImGui::GetWindowPos();
//     glm::mat4 tr{ 1.0f };
//     ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
//     auto translation = ctransform.global;
//     glm::mat4 delta;
//     if(ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::LOCAL,
//                             &ctransform.local[0][0]))
//     {
//         update_transform(entity);
//     }
//
//     ImGui::End();
// }
