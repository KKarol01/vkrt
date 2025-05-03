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
        std::vector<gfx::Vertex> gfxvertices(ag.vertex_range.count);
        for(auto i = 0u; i < gfxvertices.size(); ++i) {
            gfxvertices.at(i) = { .pos = asset.vertices.at(ag.vertex_range.offset + i).position,
                                  .nor = asset.vertices.at(ag.vertex_range.offset + i).normal,
                                  .uv = asset.vertices.at(ag.vertex_range.offset + i).uv,
                                  .tang = asset.vertices.at(ag.vertex_range.offset + i).tangent };
        }

        const auto rg = Engine::get().renderer->batch_geometry(gfx::GeometryDescriptor{
            .vertices = std::span{ gfxvertices },
            .indices = std::span{ asset.indices.begin() + ag.index_range.offset, ag.index_range.count } });
        batched_geometries.push_back(rg);
    }

    batched_materials.reserve(asset.materials.size());
    for(auto& am : asset.materials) {
        if(am.color_texture != assets::s_max_asset_index) { // todo: should be done with try_get_texture i think.
            const auto& act = asset.textures.at(am.color_texture);
            const auto rm = Engine::get().renderer->batch_material(gfx::MaterialDescriptor{
                .base_color_texture = batched_textures.at(am.color_texture) });
            batched_materials.push_back(rm);
        }
    }

    const auto parse_node_components = [&](Node& snode, assets::Node& anode) {
        if(auto* amesh = asset.try_get_mesh(anode)) {
            snode.name = amesh->name;
            snode.submeshes.reserve(amesh->submeshes.size());
            for(auto& asidx : amesh->submeshes) {
                auto& as = asset.get_submesh(asidx);
                auto* ag = asset.try_get_geometry(as);
                auto* am = asset.try_get_material(as);
                assert(ag && am);
                const auto rm = Engine::get().renderer->batch_mesh(gfx::MeshDescriptor{
                    .geometry = batched_geometries.at(as.geometry), .material = batched_materials.at(as.material) });
                snode.submeshes.push_back(rm);
            }
        }
        snode.transform = asset.get_transform(anode);
    };

    const auto parse_node = [&](assets::Node& anode, auto& recursive) -> Node* {
        Node* snode;
        add_node(&snode);
        snode->name = anode.name;
        snode->children.reserve(anode.nodes.size());
        parse_node_components(*snode, anode);
        for(auto& c : anode.nodes) {
            snode->children.push_back(recursive(asset.get_node(c), recursive));
        }
        return snode;
    };

    Node* root;
    const auto root_handle = add_node(&root);
    root->name = path.filename().string() + "_root";
    for(auto& n : asset.scene) {
        root->children.push_back(parse_node(asset.get_node(n), parse_node));
    }
    return root_handle;
}

Handle<NodeInstance> Scene::instance_model(Handle<Node> entity) { 
    const auto& sn = get_node(entity); 
    return {};
}

void Scene::update_transform(Handle<NodeInstance> entity, glm::mat4 transform) { ENG_TODO(); }

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
