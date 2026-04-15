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

#if 0

ecs::EntityId Scene::instance_model(const asset::Model* model)
{
    if(!model) { return ecs::EntityId{}; }

    static constexpr auto make_hierarchy = [](const auto& self, const asset::Model& model,
                                              const asset::Model::Node& node, ecs::EntityId parent) -> ecs::EntityId {
        auto* ecsr = get_engine().ecs;
        auto entity = ecsr->create();
        ecsr->add_components(entity, ecs::Node{ node.name, &model }, ecs::Transform{ glm::mat4{ 1.0f }, node.transform });
        if(node.mesh != ~0u) { ecsr->add_components(entity, ecs::Mesh{ &model.meshes.at(node.mesh), ~0u }); }
        if(parent) { ecsr->make_child(parent, entity); }
        for(const auto& e : node.children)
        {
            self(self, model, model.nodes.at(e), entity);
        }
        return entity;
    };

    const auto instance = make_hierarchy(make_hierarchy, *model, model->nodes.at(model->root_node), ecs::EntityId{});
    scene.push_back(instance);
    return instance;
}

void Scene::update_transform(ecs::EntityId entity)
{
    auto& ecstrs = get_engine().ecs->get<ecs::Transform>(entity);
    pending_transforms.push_back(entity);
}

void Scene::update()
{
    // Relies on pending transforms not having child nodes of other nodes (no two nodes from the same hierarchy)
    if(pending_transforms.size())
    {
        std::unordered_set<ecs::EntityId> visited;

        // leave only those entities, who have no ancestors in the pending trs.
        std::vector<ecs::EntityId> filtered;
        filtered.reserve(pending_transforms.size());
        visited.insert(pending_transforms.begin(), pending_transforms.end());
        for(auto e : pending_transforms)
        {
            auto p = e;
            auto passes = true;
            while(p)
            {
                p = get_engine().ecs->get_parent(p);
                if(visited.contains(p))
                {
                    passes = false;
                    break;
                }
            }
            if(passes) { filtered.push_back(e); }
        }
        pending_transforms = std::move(filtered);

        for(auto e : pending_transforms)
        {
            const auto p = get_engine().ecs->get_parent(e);
            std::stack<ecs::EntityId> visit;
            std::stack<glm::mat4> trs;
            visit.push(e);
            if(p)
            {
                auto& pt = get_engine().ecs->get<ecs::Transform>(p);
                trs.push(pt.global);
            }
            else { trs.push(glm::identity<glm::mat4>()); }

            while(visit.size())
            {
                ENG_ASSERT(trs.size() == visit.size());
                auto e = visit.top();
                auto pt = trs.top();
                visit.pop();
                trs.pop();
                auto& t = get_engine().ecs->get<ecs::Transform>(e);
                t.global = t.local * pt;

                ENG_ASSERT(false);
                // get_engine().renderer->update_transform(e);
                get_engine().ecs->loop_over_children(e, [&t, &trs, &visit](auto e) {
                    trs.push(t.global);
                    visit.push(e);
                });
            }
        }
        pending_transforms.clear();
    }
}

void Scene::ui_draw_scene()
{
    const auto expand_hierarchy = [this](ecs::Registry* reg, ecs::EntityId e, bool expand, const auto& self) -> void {
        ui.scene.nodes[e].expanded = expand;
        reg->loop_over_children(e, [&](ecs::EntityId ch) { self(reg, ch, expand, self); });
    };

    const auto draw_hierarchy = [&, this](ecs::Registry* reg, ecs::EntityId e, const auto& self) -> void {
        const auto enode = reg->get<ecs::Node>(e);
        ImGui::PushID((int)*e);
        auto& ui_node = ui.scene.nodes[e];
        // ImGui::BeginGroup();
        if(reg->has_children(e))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImGui::GetStyle().ItemSpacing * 0.5f);
            if(ImGui::ArrowButton("expand_btn", ui_node.expanded ? ImGuiDir_Down : ImGuiDir_Right))
            {
                ui_node.expanded = !ui_node.expanded;
            }
            ImGui::PopStyleVar(1);
            ImGui::SameLine();
        }
        {
            bool is_sel = e == ui.scene.sel_entity;
            auto cpos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(cpos + ImVec2{ -ImGui::GetStyle().ItemSpacing.x * 0.5f, 0.0f });
            ImGui::GetItemRectSize();
            if(ImGui::Selectable(enode.name.c_str(), &is_sel)) { ui.scene.sel_entity = e; }
        }
        // ImGui::EndGroup();
        if(ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(0))
        {
            expand_hierarchy(reg, e, !ui_node.expanded, expand_hierarchy);
        }

        if(ui_node.expanded)
        {
            ImGui::Indent();
            reg->loop_over_children(e, [&](ecs::EntityId ec) { self(reg, ec, self); });
            ImGui::Unindent();
        }
        ImGui::PopID();
    };
    for(const auto& e : scene)
    {
        draw_hierarchy(get_engine().ecs, e, draw_hierarchy);
    }
}

void Scene::ui_draw_inspector()
{
    if(!ui.scene.sel_entity) { return; }

    auto* ecs = get_engine().ecs;
    auto& entity = ui.scene.sel_entity;
    auto& uie = ui.scene.nodes.at(entity);
    auto& ctransform = ecs->get<ecs::Transform>(entity);
    auto& cnode = ecs->get<ecs::Node>(entity);
    auto& cmesh = ecs->get<ecs::Mesh>(entity);
    auto& clight = ecs->get<ecs::Light>(entity);

    ENG_ASSERT(false);

    // if(ImGui::Begin("Inspector", 0, ImGuiWindowFlags_HorizontalScrollbar))
    //{
    //     ENG_ASSERT(cnode && ctransform);
    //     ImGui::SeparatorText("Node");
    //     ImGui::SeparatorText("Transform");
    //     if(ImGui::DragFloat3("Position", &ctransform->local[3].x)) { update_transform(entity); }
    //     if(cmesh)
    //     {
    //         ImGui::SeparatorText("Mesh renderer");
    //         ImGui::Text(cmesh->mesh->name.c_str());
    //         if(cmesh->meshes.size())
    //         {
    //             // ImGui::Indent();
    //             for(auto& e : cmesh->meshes)
    //             {
    //                 auto& material = e->material.get();
    //                 ImGui::Text("Pass: %s", material.mesh_pass->name.c_str());
    //                 if(material.base_color_texture)
    //                 {
    //                     ImGui::Image(*material.base_color_texture + 1, { 128.0f, 128.0f });
    //                 }
    //             }
    //             // ImGui::Unindent();
    //         }
    //     }
    //     if(clight)
    //     {
    //         ImGui::SeparatorText("Light");
    //         ImGui::Text("Type: %s", to_string(clight->type).c_str());
    //         bool edited = false;
    //         edited |= ImGui::ColorEdit4("Color", &clight->color.x);
    //         edited |= ImGui::SliderFloat("Range", &clight->range, 0.0f, 100.0f);
    //         edited |= ImGui::SliderFloat("Intensity", &clight->intensity, 0.0f, 100.0f);
    //         // todo: don't like that entities with light component have to be detected and handled separately
    //         if(edited) { update_transform(entity); }
    //     }

    //    if(cmesh)
    //    {
    //        ImGui::SeparatorText("BVH");
    //        for(auto i = 0u; i < cmesh->mesh->geometries.size; ++i)
    //        {
    //            const auto& bvh = cnode->model->geometries[cmesh->mesh->geometries.offset + i].bvh;
    //            const auto stats = bvh.get_stats();
    //            ImGui::Checkbox("##bvh_level_exclusive", &uie.bvh_level_exclusive);
    //            ImGui::SameLine();
    //            if(ImGui::IsItemHovered()) { ImGui::SetItemTooltip("Shows levels up to X or only equal to X."); }
    //            ImGui::SliderInt("show level", &uie.bvh_level, 0, stats.levels);
    //            if(uie.bvh_level > 0)
    //            {
    //                for(auto ni = 0u; ni < stats.nodes.size(); ++ni)
    //                {
    //                    if((uie.bvh_level_exclusive && stats.metadatas[ni].level != uie.bvh_level) ||
    //                       (!uie.bvh_level_exclusive && stats.metadatas[ni].level > uie.bvh_level))
    //                    {
    //                        continue;
    //                    }
    //                    const auto& e = stats.nodes[ni];
    //                    get_engine().renderer->debug_bufs.add(gfx::DebugGeometry::init_aabb(e.aabb.min, e.aabb.max));
    //                }
    //            }

    //            ImGui::Text("BVH%u: size[kB]: %llu, tris: %u, nodes: %u", i, stats.size / 1024,
    //                        (uint32_t)stats.tris.size(), (uint32_t)stats.nodes.size());
    //            const auto aabb = stats.nodes[0].aabb;
    //            ImGui::Text("\tExtent:");
    //            ImGui::Text("\t[%5.2f %5.2f %5.2f]", aabb.min.x, aabb.min.y, aabb.min.z);
    //            ImGui::Text("\t[%5.2f %5.2f %5.2f]", aabb.max.x, aabb.max.y, aabb.max.z);
    //        }
    //    }
    //}
    // ImGui::End();
}

//void Scene::ui_draw_manipulate()
//{
//    if(!ui.scene.sel_entity) { return; }
//
//    auto* ecs = get_engine().ecs;
//    auto& entity = ui.scene.sel_entity;
//    auto& ctransform = ecs->get<ecs::Transform>(entity);
//    auto& cnode = ecs->get<ecs::Node>(entity);
//    auto& cmesh = ecs->get<ecs::Mesh>(entity);
//
//    auto& io = ImGui::GetIO();
//    auto& style = ImGui::GetStyle();
//    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0u); // don't set no background, make host dock push style with no bg, and somehow it works -
//                                                  // the content window actually does not have the background
//    ImGui::Begin("Manipulate", 0, ImGuiWindowFlags_NoDecoration);
//    ImGui::PopStyleColor(1);
//    ImGuizmo::SetDrawlist();
//
//    const auto view = get_engine().camera->get_view();
//    auto proj = get_engine().camera->get_projection(); // imguizmo hates inf_revz_zo perspective matrix that i use (div by 0 because no far plane)
//    proj = glm::perspectiveFov(glm::radians(75.0f), get_engine().window->width, get_engine().window->height, 0.1f, 30.0f);
//    const auto window_width = ImGui::GetWindowWidth();
//    const auto window_height = ImGui::GetWindowHeight();
//    const auto window_pos = ImGui::GetWindowPos();
//    glm::mat4 tr{ 1.0f };
//    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
//    auto translation = ctransform.global;
//    glm::mat4 delta;
//    if(ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::LOCAL,
//                            &ctransform.local[0][0]))
//    {
//        update_transform(entity);
//    }
//
//    ImGui::End();
//}
} // namespace eng

#endif