#pragma once

#include <filesystem>
#include <map>
#include "handle.hpp"
#include "sorted_vec.hpp"
#include "renderer.hpp"

class Scene {
  public:
    struct ModelAsset {
        struct Mesh {
            std::string name;
        };

        std::string name;
        std::filesystem::path path;
        Handle<BatchedRenderModel> render_handle;
        std::vector<Mesh> meshes;
    };

    struct ModelInstance {
        constexpr bool operator<(const ModelInstance& a) const noexcept {
            if(asset <= a.asset) { return true; }
            return handle < a.handle;
        }

        Handle<ModelAsset> asset;
        Handle<ModelInstance> handle;
    };

    Handle<ModelAsset> load_from_file(const std::filesystem::path& path);
    Handle<ModelInstance> instance_model(Handle<ModelAsset> asset, InstanceSettings settings);

    std::unordered_map<Handle<ModelAsset>, ModelAsset> model_assets;
    SortedVector<ModelInstance> model_instances;
};