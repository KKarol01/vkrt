#include <stack>
#include <ranges>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb/stb_image.h>
#include <eng/scene.hpp>
#include <eng/engine.hpp>
#include <eng/common/components.hpp>
#include <eng/assets/importer.hpp>
#include <eng/common/logger.hpp>

namespace scene {

Handle<Node> Scene::load_from_file(const std::filesystem::path& path) {
    if(path.extension() != ".glb") {
        assert(false && "Only .glb files are supported.");
        return {};
    }

    const std::filesystem::path full_path =
        (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "models" / path).lexically_normal();
    auto asset = assets::Importer::import_glb(full_path);
    if(asset.scene.size() == 0) {
        ENG_WARN("Imported model's scene has empty scene.");
        return {};
    }

    return load_from_asset(asset);
}

Handle<Node> Scene::load_from_asset(assets::Asset& asset) {
    std::vector<Handle<gfx::Image>> batched_images;
    std::vector<Handle<gfx::Texture>> batched_textures;
    std::vector<Handle<gfx::Geometry>> batched_geometries;
    std::vector<Handle<gfx::Material>> batched_materials;

    batched_images.reserve(asset.images.size());
    for(auto& ai : asset.images) {
        const auto ri = Engine::get().renderer->batch_image(gfx::ImageDescriptor{
            .name = ai.name, .width = ai.width, .height = ai.height, .format = ai.format, .data = ai.data });
        batched_images.push_back(ri);
    }

    batched_textures.reserve(asset.textures.size());
    for(auto& at : asset.textures) {
        const auto rt = Engine::get().renderer->batch_texture(gfx::TextureDescriptor{
            .image = batched_images.at(at.image), .filtering = at.filtering, .addressing = at.addressing });
        batched_textures.push_back(rt);
    }

    batched_geometries.reserve(asset.geometries.size());
    for(auto& ag : asset.geometries) {
        std::vector<gfx::Vertex> gfxvertices(ag.vertex_range.size);
        std::vector<gfx::Meshlet> gfxmeshlets(ag.index_range.size);
        std::vector<uint32_t> meshlets_vertices;
        std::vector<gfx::Meshlet> gfxmeshlets(ag.index_range.size);
        for(auto i = 0u; i < gfxvertices.size(); ++i) {
            gfxvertices.at(i) = { .pos = asset.vertices.at(ag.vertex_range.offset + i).position,
                                  .nor = asset.vertices.at(ag.vertex_range.offset + i).normal,
                                  .uv = asset.vertices.at(ag.vertex_range.offset + i).uv,
                                  .tang = asset.vertices.at(ag.vertex_range.offset + i).tangent };
        }
        for(auto i = 0u; i < gfxmeshlets.size(); ++i) {
            gfxmeshlets.at(i) = { .vertex_range = asset.meshlets.at(ag.index_range.offset + i).vertex_range,
                                  .triangle_range = asset.meshlets.at(ag.index_range.offset + i).triangle_range };
        }

        Handle<gfx::Geometry> rg;
        if(asset.meshlets.empty()) {
            rg = Engine::get().renderer->batch_geometry(gfx::GeometryDescriptor{
                .vertices = std::span{ gfxvertices },
                .indices = std::span{ asset.indices.begin() + ag.index_range.offset, ag.index_range.size },
            });
        } else {
            rg = Engine::get().renderer->batch_geometry(gfx::GeometryDescriptor{
                .vertices = std::span{ gfxvertices },
                .indices = std::span{},
                .meshlets = std::span{ gfxmeshlets },
                .meshlets_triangles = 
                std::span{ asset.indices.begin() + ag.index_range.offset, ag.index_range.size },
            });
        }
        batched_geometries.push_back(rg);
    }

    batched_materials.reserve(asset.materials.size());
    for(auto& am : asset.materials) {
        if(am.color_texture != assets::s_max_asset_index) { // todo: should be done with try_get_texture i think.
            const auto rm = Engine::get().renderer->batch_material(gfx::MaterialDescriptor{
                .base_color_texture = am.color_texture == assets::s_max_asset_index ? Handle<gfx::Texture>{}
                                                                                    : batched_textures.at(am.color_texture) });
            batched_materials.push_back(rm);
        }
    }

    const auto parse_node_components = [&](Node& snode, assets::Node& anode) {
        if(auto* amesh = asset.try_get_mesh(anode)) {
            snode.mesh = Mesh{};
            snode.mesh->name = amesh->name;
            snode.mesh->submeshes.reserve(amesh->submeshes.size());
            for(auto& asidx : amesh->submeshes) {
                auto& as = asset.get_submesh(asidx);
                auto* ag = asset.try_get_geometry(as);
                auto* am = asset.try_get_material(as);
                // assert(ag && am);
                const auto rm = Engine::get().renderer->batch_mesh(gfx::MeshDescriptor{
                    .geometry = batched_geometries.at(as.geometry),
                    .material = am ? batched_materials.at(as.material) : Handle<gfx::Material>{} });
                snode.mesh->submeshes.push_back(rm);
            }
        }
        snode.transform = asset.get_transform(anode);
    };

    const auto parse_node = [&](assets::Node& anode, auto& recursive) -> Handle<Node> {
        Node* snode;
        const auto handle = add_node(&snode);
        snode->name = anode.name;
        snode->transform = asset.get_transform(anode);
        snode->children.reserve(anode.nodes.size());
        parse_node_components(*snode, anode);
        for(auto& c : anode.nodes) {
            snode->children.push_back(recursive(asset.get_node(c), recursive));
        }
        return handle;
    };

    Node* rn;
    const auto hrn = add_node(&rn);
    rn->name = fmt::format("{}", asset.path.filename().replace_extension().string());
    rn->children.reserve(asset.scene.size());
    for(auto& sn : asset.scene) {
        rn->children.push_back(parse_node(asset.get_node(sn), parse_node));
    }
    return hrn;
}

Handle<NodeInstance> Scene::instance_model(Handle<Node> node) {
    auto& sn = get_node(node);
    std::stack<NodeInstance*> nis;
    NodeInstance* rni;
    const auto hrni = add_instance(&rni);
    nis.push(rni);
    traverse_dfs(node, [&](Node& n) {
        auto* ni = nis.top();
        nis.pop();
        ni->name = n.name;
        ni->entity = Engine::get().ecs_system->create();
        Engine::get().ecs_system->emplace<components::Transform>(ni->entity)->transform = n.transform;
        if(n.mesh) {
            auto& cm = *Engine::get().ecs_system->emplace<components::Mesh>(ni->entity);
            cm.name = n.mesh->name;
            cm.submeshes.reserve(n.mesh->submeshes.size());
            for(const auto& sm : n.mesh->submeshes) {
                cm.submeshes.push_back(sm);
            }
            Engine::get().renderer->instance_mesh(gfx::InstanceSettings{ .entity = ni->entity });
        }
        ni->children.reserve(n.children.size());
        for(auto& nc : std::views::reverse(n.children)) {
            NodeInstance* nci;
            const auto hnci = add_instance(&nci);
            ni->children.push_back(hnci);
            nis.push(nci);
        }
    });
    return hrni;
}

void Scene::update_transform(Handle<NodeInstance> entity, glm::mat4 transform) {
    ENG_TODO();
    const auto ent = instance_handles.at(entity);
    Engine::get().ecs_system->get<components::Transform>(ent->entity).transform = transform;
    for(auto& e : ent->children) {
        Engine::get().ecs_system->get<components::Transform>(instance_handles.at(e)->entity).transform = transform;
    }
}

Handle<Node> Scene::add_node(Node** out) {
    auto& n = nodes.emplace_back();
    const auto handle = Handle<Node>{ generate_handle };
    node_handles[handle] = &n;
    if(out) { *out = &n; }
    return handle;
}

Handle<NodeInstance> Scene::add_instance(NodeInstance** out) {
    auto& n = node_instances.emplace_back();
    const auto handle = Handle<NodeInstance>{ generate_handle };
    instance_handles[handle] = &n;
    if(out) { *out = &n; }
    return handle;
}

} // namespace scene

// void Scene::update_transform(Handle<Entity> entity) {
//  uint32_t idx = entity_node_idxs.at(entity);
//  glm::mat4 tr = glm::mat4{ 1.0f };
//  if(nodes.at(idx).parent != ~0u) { tr = final_transforms.at(nodes.at(idx).parent); }
//_update_transform(idx, tr);
//}

// void Scene::_update_transform(uint32_t idx, glm::mat4 t) {
//  Node& node = nodes.at(idx);
//  cmps::Transform& tr = Engine::ec()->get<cmps::Transform>(node.handle);
//  final_transforms.at(idx) = tr.transform * t;
//  if(node.has_component<cmps::Mesh>()) {
//      Engine::get().renderer->update_transform(Engine::ec()->get<cmps::Mesh>(node.handle).ri_handle);
//  }
//  for(uint32_t i = 0; i < node.children_count; ++i) {
//      _update_transform(node.children_offset + i, final_transforms.at(idx));
//  }
//}

// bool NodeInstance::has_children() const {
//     //for(const auto& e : children) {
//     //    if(e) { return true; }
//     //}
//     //return false;
//     return
// }
