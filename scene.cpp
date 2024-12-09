#include <stack>
#include <functional>
#include "scene.hpp"
#include "model_importer.hpp"
#include "engine.hpp"
#include "common/components.hpp"

Handle<ModelAsset> Scene::load_from_file(const std::filesystem::path& path) {
    if(auto it = path_model_assets.find(path); it != path_model_assets.end()) { return it->second; }

    ImportedModel model = ModelImporter::import_model(path);

    std::vector<Vertex> vertices;
    vertices.reserve(model.vertices.size());
    std::transform(model.vertices.begin(), model.vertices.end(), std::back_inserter(vertices), [](const ImportedModel::Vertex& v) {
        return Vertex{
            .pos = v.pos,
            .nor = v.nor,
            .uv = v.uv,
            .tang = v.tang,
        };
    });
    Handle<RenderGeometry> geometry_handle = Engine::renderer()->batch_geometry(GeometryDescriptor{
        .vertices = vertices,
        .indices = model.indices,
    });

    std::vector<Handle<Image>> textures;
    std::vector<ImageFormat> texture_formats(model.textures.size());
    for(const auto& e : model.materials) {
        if(e.color_texture) { texture_formats.at(*e.color_texture) = ImageFormat::SRGB; }
        // Rest of the types should be unorm (for now).
    }

    for(uint32_t i = 0; i < model.textures.size(); ++i) {
        auto& e = model.textures.at(i);
        textures.push_back(Engine::renderer()->batch_texture(ImageDescriptor{
            .name = e.name,
            .width = e.size.first,
            .height = e.size.second,
            .format = texture_formats.at(i),
            .data = e.rgba_data,
        }));
    }

    std::vector<MaterialAsset> materials;
    for(const auto& e : model.materials) {
        Handle<RenderMaterial> material_handle = Engine::renderer()->batch_material(MaterialDescriptor{
            .base_color_texture = textures.at(e.color_texture.value_or(0)),
            .normal_texture = e.normal_texture ? textures.at(*e.normal_texture) : Handle<Image>{},
            .metallic_roughness_texture = e.metallic_roughness_texture ? textures.at(*e.metallic_roughness_texture) : Handle<Image>{},
        });
        materials.push_back(MaterialAsset{
            .material_handle = material_handle,
            .color_texture_handle = e.color_texture ? textures.at(*e.color_texture) : Handle<Image>{},
            .normal_texture_handle = e.normal_texture ? textures.at(*e.normal_texture) : Handle<Image>{},
            .metallic_roughness_texture_handle =
                e.metallic_roughness_texture ? textures.at(*e.metallic_roughness_texture) : Handle<Image>{},
        });
    }

    std::vector<MeshAsset> meshes;
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
        meshes.push_back(MeshAsset{
            .name = e.name,
            .rm_handle = mesh_handle,
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
    handle_model_assets[asset_handle] = &model_assets.back();
    path_model_assets[path] = asset_handle;
    return asset_handle;
}

Handle<Node> Scene::instance_model(Handle<ModelAsset> asset, InstanceSettings settings) {
    ModelAsset& ma = *handle_model_assets.at(asset);
    Node parent{
        .name = ma.path.filename().replace_extension().string(),
        .handle = Handle<Entity>{ generate_handle },
        .children_offset = (uint32_t)nodes.size() + 1,
        .children_count = (uint32_t)ma.meshes.size(),
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
        attach_component<cmps::Transform>(&child, cmps::Transform{ .transform = glm::mat4{ 1.0f } });
        attach_component<cmps::RenderMesh>(&child, cmps::RenderMesh{ .asset = &ma, .mesh = &e });

        Engine::renderer()->instance_mesh(InstanceSettings{
            .flags = settings.flags,
            .entity = child.handle,
            .material = e.material->material_handle,
            .mesh = e.rm_handle,
        });

        if(settings.flags.test(InstanceFlags::RAY_TRACED_BIT)) {
            Engine::renderer()->instance_blas(BLASInstanceSettings{ .entity = child.handle });
        }
        entity_node_idxs[child.handle] = nodes.size();
        nodes.push_back(child);
        final_transforms.push_back(settings.transform);
    }

    return Handle<Node>{ *parent.handle };
}

void Scene::update_transform(Handle<Entity> entity) {
    uint32_t idx = entity_node_idxs.at(entity);
    glm::mat4 tr = glm::mat4{ 1.0f };
    if(nodes.at(idx).parent != ~0u) { tr = final_transforms.at(nodes.at(idx).parent); }
    _update_transform(idx, tr);
}

void Scene::_update_transform(uint32_t idx, glm::mat4 t) {
    Node& node = nodes.at(idx);
    cmps::Transform& tr = Engine::ec()->get<cmps::Transform>(node.handle);
    final_transforms.at(idx) = tr.transform * t;
    if(node.has_component<cmps::RenderMesh>()) {
        Engine::renderer()->update_transform(Engine::ec()->get<cmps::RenderMesh>(node.handle).ri_handle);
    }
    for(uint32_t i = 0; i < node.children_count; ++i) {
        _update_transform(node.children_offset + i, final_transforms.at(idx));
    }
}

template <typename Comp> Comp& Scene::attach_component(Node* node, Comp&& comp) {
    node->components |= (1u << EntityComponentIdGenerator<>::get_id<Comp>());
    Engine::ec()->insert<Comp>(node->handle, std::forward<Comp>(comp));
    return Engine::ec()->get<Comp>(node->handle);
}
