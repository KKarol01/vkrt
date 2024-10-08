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
        BoundingBox aabb;
        for(uint32_t i = e.vertex_offset; i < e.vertex_offset + e.vertex_count; ++i) {
            aabb.min = glm::min(aabb.min, vertices.at(i).pos);
            aabb.max = glm::max(aabb.max, vertices.at(i).pos);
        }

        meshes.push_back(ModelAsset::Mesh{
            .name = e.name,
            .mesh_handle = Engine::renderer()->batch_mesh(MeshDescriptor{ .geometry = geometry_handle,
                                                                          .vertex_offset = e.vertex_offset,
                                                                          .index_offset = e.index_offset,
                                                                          .vertex_count = e.vertex_count,
                                                                          .index_count = e.index_count }),
            .material = e.material.value_or(0),
            .aabb = aabb });
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

Handle<Scene::ModelInstance> Scene::instance_model(Handle<ModelAsset> asset, InstanceSettings settings) {
    ModelAsset& ma = *model_asset_handles.at(asset);
    ModelInstance m_instance{ .name = settings.name.empty() ? ma.path.filename().replace_extension().string()
                                                            : std::move(settings.name),
                              .instance_offset = model_instances.size(),
                              .instance_count = ma.meshes.size(),
                              .model = &ma,
                              .transform = settings.transform };
    transforms.reserve(transforms.size() + m_instance.instance_count);

    Handle<Scene::ModelInstance> mi_handle = model_instances.insert(std::move(m_instance));

    for(auto& e : ma.meshes) {
        mesh_instances.push_back(MeshInstance{
            .mesh = &e,
            .model_instance = mi_handle,
            .renderer_handle = Engine::renderer()->instance_mesh(InstanceSettings{
                .flags = settings.flags, .mesh = e.mesh_handle, .material = ma.materials.at(e.material).material_handle }) });
        transforms.push_back(glm::mat4{ 1.0f });
        if(settings.flags.test(InstanceFlags::RAY_TRACED_BIT)) {
            Engine::renderer()->instance_blas(BLASInstanceSettings{ .mesh_instance = mesh_instances.back().renderer_handle });
        }
    }

    return mi_handle;
}
