#include "scene.hpp"
#include "model_importer.hpp"
#include "engine.hpp"

Handle<Scene::ModelAsset> Scene::load_from_file(const std::filesystem::path& path) {
    ImportedModel model = ModelImporter::import_model(path);

    std::vector<Vertex> vertices;
    vertices.reserve(model.vertices.size());
    std::transform(model.vertices.begin(), model.vertices.end(), std::back_inserter(vertices),
                   [](const ImportedModel::Vertex& v) { return Vertex{ .pos = v.pos, .nor = v.nor, .uv = v.uv }; });
    Handle<GeometryBatch> geometry_handle = Engine::renderer()->batch_geometry(GeometryDescriptor{
        .vertices = vertices,
        .indices = model.indices,
    });

    std::vector<Handle<TextureBatch>> textures;
    for(const auto& e : model.textures) {
        textures.push_back(Engine::renderer()->batch_texture(TextureBatch{
            .name = e.name,
            .width = e.size.first,
            .height = e.size.second,
            .data = e.rgba_data,
        }));
    }

    std::vector<ModelAsset::Material> materials;
    for(const auto& e : model.materials) {
        Handle<MaterialBatch> material_handle =
            Engine::renderer()->batch_material(MaterialBatch{ .color_texture = textures.at(e.color_texture.value_or(0)) });
        materials.push_back(ModelAsset::Material{ .name = e.name,
                                                  .material_handle = material_handle,
                                                  .color_texture_handle = textures.at(e.color_texture.value_or(0)) });
    }

    std::vector<ModelAsset::Mesh> meshes;
    for(const auto& e : model.meshes) {
        meshes.push_back(ModelAsset::Mesh{
            .name = e.name,
            .mesh_handle = Engine::renderer()->batch_mesh(MeshDescriptor{ .geometry = geometry_handle,
                                                                          .vertex_offset = e.vertex_offset,
                                                                          .index_offset = e.index_offset,
                                                                          .vertex_count = e.vertex_count,
                                                                          .index_count = e.index_count }),
            .material = e.material.value_or(0) });
    }

    ModelAsset asset{ .name = path.filename().replace_extension().string(),
                      .path = path,
                      .geometry = geometry_handle,
                      .meshes = std::move(meshes),
                      .materials = std::move(materials),
                      .textures = std::move(textures) };
    Handle<ModelAsset> asset_handle{ generate_handle };
    model_assets.emplace(asset_handle, std::move(asset));

    return asset_handle;
}

Handle<Scene::ModelInstance> Scene::instance_model(Handle<ModelAsset> asset, InstanceSettings settings) {
    ModelAsset& ma = model_assets.at(asset);
    ModelInstance instance{ .asset = asset };
    for(const auto& e : ma.meshes) {
        instance.mesh_instance_handles.push_back(Engine::renderer()->instance_mesh(InstanceSettings{
            .flags = settings.flags, .mesh = e.mesh_handle, .material = ma.materials.at(e.material).material_handle }));
    }

    if(settings.flags.test(InstanceFlags::RAY_TRACED_BIT)) {
        Engine::renderer()->instance_blas(BLASInstanceSettings{ .geometry = ma.geometry,
                                                                .mesh_instances = instance.mesh_instance_handles });
    }

    return model_instances.insert(std::move(instance));
}
