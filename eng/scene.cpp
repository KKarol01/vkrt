#include <stack>
#include <functional>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb/stb_image.h>
#include <eng/scene.hpp>
#include <eng/model_importer.hpp>
#include <eng/engine.hpp>
#include <eng/common/components.hpp>
// #include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/assets/importer.hpp>

namespace scene {

Handle<Node> Scene::load_from_file(const std::filesystem::path& path) {
    if(path.extension() != ".glb") {
        assert(false && "Only .glb files are supported.");
        return {};
    }
    const std::filesystem::path full_path = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "models" / path;
    auto asset = assets::Importer::import_glb(full_path);

    const auto parse_node_components = [&](Node& snode, assets::Node& anode) {
        if(auto* amesh = asset.try_get_mesh(anode)) {
            auto& cmesh = Engine::get().ecs_storage->get<components::Mesh>(snode.entity);
            cmesh.name = amesh->name;
            cmesh.submeshes.reserve(amesh->submeshes.size());
            for(auto& asidx : amesh->submeshes) {
                auto& as = asset.get_submesh(asidx);
                auto* ag = asset.try_get_geometry(as);
                auto* am = asset.try_get_material(as);
                assert(ag && am);
                std::vector<gfx::Vertex> gfxvertices(ag->vertex_range.count);
                for(auto i = 0u; i < gfxvertices.size(); ++i) {
                    gfxvertices.at(i) = { .pos = asset.vertices.at(ag->vertex_range.offset + i).position,
                                          .nor = asset.vertices.at(ag->vertex_range.offset + i).normal,
                                          .uv = asset.vertices.at(ag->vertex_range.offset + i).uv,
                                          .tang = asset.vertices.at(ag->vertex_range.offset + i).tangent };
                }

                const auto rg = Engine::get().renderer->batch_geometry(gfx::GeometryDescriptor{
                    .vertices = std::span{ gfxvertices },
                    .indices = std::span{ asset.indices.begin() + ag->index_range.offset, ag->index_range.count } });
                Engine::get().renderer->batch_texture()
            }
        }
    };

    const auto parse_node = [&](assets::Node& anode, auto& recursive) -> Node* {
        Node snode;
        snode.name = anode.name;
        snode.entity = Engine::get().ecs_storage->create();
        snode.children.reserve(anode.nodes.size());
        for(auto& c : anode.nodes) {
            snode.children.push_back(recursive(asset.get_node(c), recursive));
        }
        nodes.push_back(std::move(snode));
        return &nodes.back();
    };

    for(auto& n : asset.scene) {
        parse_node(asset.get_node(n), parse_node);
    }
}

Handle<NodeInstance> Scene::instance_model(Handle<Node> entity) {
    Node* n = node_handles.at(entity);
    NodeInstance* i = add_instance();
    std::stack<NodeInstance*> i_stack;
    i_stack.push(i);
    traverse_node_hierarchy_indexed(n, [&](Node* n, uint32_t idx) {
        NodeInstance* ni = i_stack.top();
        i_stack.pop();
        ni->name = n->name;
        ni->node_handle = n->handle;
        ni->instance_handle = Handle<NodeInstance>{ generate_handle };
        ni->transform = n->transform;
        ni->final_transform = n->transform;
        ni->children.reserve(n->children.size());
        ni->primitives.reserve(n->primitives.size());
        instance_handles[ni->instance_handle] = ni;
        for(auto& p : n->primitives) {
            auto pi = ni->primitives.emplace_back(Engine::get().ecs_storage->create());
            Engine::get().ecs_storage->emplace<components::Transform>(pi, ni->final_transform);
            Engine::get().ecs_storage->emplace<components::Renderable>(
                pi, components::Renderable{ .mesh_handle = p.mesh_handle, .material_handle = p.material_handle });
            Engine::get().renderer->instance_mesh(gfx::InstanceSettings{ .entity = pi });
            Engine::get().renderer->instance_blas(gfx::BLASInstanceSettings{ .entity = pi });
        }
        for(auto& c : n->children) {
            ni->children.push_back(add_instance());
        }
        for(auto it = ni->children.rbegin(); it != ni->children.rend(); ++it) {
            i_stack.push(*it);
        }
    });
    scene.push_back(i);
    return i->instance_handle;
}

void Scene::update_transform(Handle<NodeInstance> entity, glm::mat4 transform) {
    auto* instance = instance_handles.at(entity);
    std::stack<glm::mat4> stack;
    // stack.push(glm::mat4{1.0f});
    stack.push(transform);
    traverse_node_hierarchy_indexed(instance, [&stack](auto* node, auto idx) {
        // remove node's transform, leave parent transform.
        node->final_transform = glm::inverse(node->transform) * node->final_transform;
        // update to new transform, calc new final transform, pass down.
        node->transform = stack.top();
        stack.pop();
        node->final_transform = node->transform * node->final_transform;
        for(auto* c : node->children) {
            if(c) { stack.push(node->final_transform); }
        }
        for(auto p : node->primitives) {
            if(p != components::s_max_entity) {
                Engine::get().ecs_storage->get<components::Transform>(p).transform = node->final_transform;
                Engine::get().renderer->update_transform(p);
            }
        }
    });
}

Node* Scene::add_node() {
    auto& n = nodes.emplace_back();
    n.handle = Handle<Node>{ generate_handle };
    node_handles[n.handle] = &n;
    return &n;
}

NodeInstance* Scene::add_instance() {
    auto& n = node_instances.emplace_back();
    n.instance_handle = Handle<NodeInstance>{ generate_handle };
    return &n;
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

bool NodeInstance::has_children() const {
    for(const auto& e : children) {
        if(e) { return true; }
    }
    return false;
}
