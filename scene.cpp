#include "scene.hpp"
#include "model_importer.hpp"
#include "engine.hpp"

Handle<Scene::ModelAsset> Scene::load_from_file(const std::filesystem::path& path) {
    ImportedModel model = ModelImporter::import_model(path);
    Handle<BatchedRenderModel> render_handle = Engine::renderer()->batch_model(model, { BatchFlags::RAY_TRACED_BIT });

    ModelAsset asset{ path.filename().replace_extension().string(), path, render_handle };
    for(const auto& e : model.meshes) {
        asset.meshes.push_back(ModelAsset::Mesh{ e.name });
    }

    Handle<ModelAsset> handle{ generate_handle };
    model_assets.emplace(handle, asset);
    return handle;
}

Handle<Scene::ModelInstance> Scene::instance_model(Handle<ModelAsset> asset, InstanceSettings settings) {
    const auto& model = model_assets.at(asset);
    Handle<InstancedRenderModel> instance_render_handle = Engine::renderer()->instance_model(model.render_handle, settings);
    Handle<ModelInstance> instance_handle{ generate_handle };
    model_instances.emplace(asset, instance_handle);
    return instance_handle;
}
