#include <stack>
#include <functional>
#include "scene.hpp"
#include "model_importer.hpp"
#include "engine.hpp"
#include "common/components.hpp"

Handle<ModelAsset> Scene::load_from_file(const std::filesystem::path& path) {
    ImportedModel model = ModelImporter::import_model(path);

    std::vector<Vertex> vertices;
    vertices.reserve(model.vertices.size());
    std::transform(model.vertices.begin(), model.vertices.end(), std::back_inserter(vertices),
                   [](const ImportedModel::Vertex& v) { return Vertex{ .pos = v.pos, .nor = v.nor, .uv = v.uv }; });
    Handle<RenderGeometry> geometry_handle = Engine::renderer()->batch_geometry(GeometryDescriptor{
        .vertices = vertices,
        .indices = model.indices,
    });

    std::vector<Handle<RenderTexture>> textures;
    for(const auto& e : model.textures) {
        textures.push_back(Engine::renderer()->batch_texture(RenderTexture{
            .name = e.name,
            .width = e.size.first,
            .height = e.size.second,
            .data = e.rgba_data,
        }));
    }

    std::vector<Material> materials;
    for(const auto& e : model.materials) {
        Handle<MaterialBatch> material_handle =
            Engine::renderer()->batch_material(MaterialBatch{ .color_texture = textures.at(e.color_texture.value_or(0)) });
        materials.push_back(Material{ .name = e.name,
                                      .material_handle = material_handle,
                                      .color_texture_handle = textures.at(e.color_texture.value_or(0)) });
    }

    std::vector<Mesh> meshes;
    for(auto& e : model.meshes) {
        BoundingBox aabb;
        for(uint32_t i = e.vertex_offset; i < e.vertex_offset + e.vertex_count; ++i) {
            aabb.min = glm::min(aabb.min, vertices.at(i).pos);
            aabb.max = glm::max(aabb.max, vertices.at(i).pos);
        }
        Handle<RenderMesh> mesh_handle = Engine::renderer()->batch_mesh(MeshDescriptor{
            .geometry = geometry_handle,
            .vertex_offset = e.vertex_offset,
            .index_offset = e.index_offset,
            .vertex_count = e.vertex_count,
            .index_count = e.index_count,
        });
        meshes.push_back(Mesh{
            .name = e.name,
            .mesh_handle = mesh_handle,
            .material = &materials.at(e.material.value_or(0)),
            .aabb = aabb,
        });
    }

    Handle<ModelAsset> asset_handle{ generate_handle };
    model_assets.push_back(ModelAsset{ .path = path,
                                       .geometry = geometry_handle,
                                       .meshes = std::move(meshes),
                                       .materials = std::move(materials),
                                       .textures = std::move(textures) });
    model_asset_handles[asset_handle] = &model_assets.back();
    return asset_handle;
}

Handle<Node> Scene::instance_model(Handle<ModelAsset> asset, InstanceSettings settings) {
    ModelAsset& ma = *model_asset_handles.at(asset);
    Node parent{
        .name = settings.name.empty() ? ma.path.filename().replace_extension().string() : std::move(settings.name),
        .handle = Handle<Entity>{ generate_handle },
        .children_offset = (u32)nodes.size() + 1,
        .children_count = (u32)ma.meshes.size(),
    };
    attach_component<cmps::Transform>(&parent, cmps::Transform{ settings.transform });
    root_nodes.push_back(nodes.size());
    entity_node_idxs[parent.handle] = nodes.size();
    nodes.push_back(parent);
    final_transforms.push_back(settings.transform);

    for(auto& e : ma.meshes) {
        Node child{
            .name = e.name,
            .handle = Handle<Entity>{ generate_handle },
            .parent = root_nodes.back(),
        };

        Handle<MeshInstance> render_handle = Engine::renderer()->instance_mesh(InstanceSettings{
            .flags = settings.flags,
            .entity = child.handle,
            .mesh = e.mesh_handle,
            .material = e.material->material_handle,
        });

        attach_component<cmps::Transform>(&child, {});
        attach_component<cmps::RenderMesh>(&child, cmps::RenderMesh{ .asset = &ma, .mesh = &e, .render_handle = render_handle });
        if(settings.flags.test(InstanceFlags::RAY_TRACED_BIT)) {
            Engine::renderer()->instance_blas(BLASInstanceSettings{ .render_instance = render_handle });
        }
        entity_node_idxs[child.handle] = nodes.size();
        nodes.push_back(child);
        final_transforms.push_back(settings.transform);
    }

    return Handle<Node>{ *parent.handle };
}

void Scene::update_transform(Handle<Entity> entity) {
    u32 idx = entity_node_idxs.at(entity);
    glm::mat4 tr = glm::mat4{ 1.0f };
    if(nodes.at(idx).parent != ~0u) { tr = final_transforms.at(nodes.at(idx).parent); }
    _update_transform(idx, tr);
}

void Scene::_update_transform(u32 idx, glm::mat4 t) {
    Node& node = nodes.at(idx);
    cmps::Transform& tr = Engine::ec()->get<cmps::Transform>(node.handle);
    final_transforms.at(idx) = tr.transform * t;
    for(u32 i = 0; i < node.children_count; ++i) {
        _update_transform(node.children_offset + i, final_transforms.at(idx));
    }
}

template <typename Comp> Comp& Scene::attach_component(Node* node, Comp&& comp) {
    node->components |= (1u << EntityComponentIdGenerator<>::get_id<Comp>());
    Engine::ec()->insert<Comp>(node->handle, std::forward<Comp>(comp));
    return Engine::ec()->get<Comp>(node->handle);
}