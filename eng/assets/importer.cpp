#include "importer.hpp"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/math.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb/stb_image.h>
#include <eng/logger.hpp>
#include <stack>
#include <variant>

namespace assets {

const Material* Asset::try_get_material(const Mesh& mesh) const {
    assert(false);
    return nullptr;
}
const Geometry* Asset::try_get_geometry(const Mesh& mesh) const {
    assert(false);
    return nullptr;
}

const Node& assets::Asset::get_node(asset_index_t idx) const { return nodes.at(idx); }

glm::mat4x3 assets::Asset::get_transform(const Node& node) const { return transforms.at(node.transform); }

Asset Importer::import_glb(const std::filesystem::path& path, ImportSettings settings) {
    Asset asset;

    const std::filesystem::path full_path = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "models" / path;
    fastgltf::Parser parser;
    auto glbbuffer = fastgltf::GltfDataBuffer::FromPath(full_path);
    if(!glbbuffer) {
        ENG_WARN("Error during fastgltf::GltfDataBuffer import: {}", fastgltf::getErrorName(glbbuffer.error()));
        return asset;
    }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    auto _gltfasset = parser.loadGltfBinary(glbbuffer.get(), full_path.parent_path(), gltfOptions);
    if(!_gltfasset) {
        ENG_WARN("Error during loading fastgltf::Parser::loadGltfBinary: {}", fastgltf::getErrorName(_gltfasset.error()));
        return asset;
    }
    auto& gltfasset = _gltfasset.get();
    auto& scene = gltfasset.scenes.at(0);

    asset.path = full_path;
    asset.nodes.reserve(gltfasset.nodes.size());
    asset.transforms.reserve(gltfasset.nodes.size());
    asset.images.reserve(gltfasset.images.size());
    asset.textures.reserve(gltfasset.textures.size());
    asset.meshes.reserve(gltfasset.meshes.size());
    asset.materials.reserve(gltfasset.materials.size());

    const auto load_image = [&gltfasset, &asset](uint32_t index, gfx::ImageFormat format) {
        auto& img = gltfasset.images.at(index);
        if(!std::holds_alternative<fastgltf::sources::BufferView>(img.data)) {
            ENG_ERROR("Image {} not loaded. Invalid image data source.", img.name);
            return;
        }
        auto& bview = gltfasset.bufferViews.at(std::get<fastgltf::sources::BufferView>(img.data).bufferViewIndex);
        auto& buff = gltfasset.buffers.at(bview.bufferIndex);
        if(!std::holds_alternative<fastgltf::sources::Array>(buff.data)) {
            ENG_ERROR("Image {} not loaded. Invalid image data source.");
            asset.images.emplace_back();
            return;
        }
        auto& arr = std::get<fastgltf::sources::Array>(buff.data);
        std::byte* data{};
        int x, y, ch;
        data = reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<stbi_uc*>(arr.bytes.data()) + bview.byteOffset,
                                                                  bview.byteLength, &x, &y, &ch, 4));
        if(!data) {
            ENG_ERROR("Image {} not loaded. Stbi failed.");
            asset.images.emplace_back();
            return;
        }
        asset.images.push_back(Image{
            .name = img.name.c_str(),
            .width = (uint32_t)x,
            .height = (uint32_t)y,
            .format = format,
            .data = { data, data + x * y * ch },
        });
    };

    for(auto sidx : scene.nodeIndices) {

        dfs_traverse_node_hierarchy(asset, )
    }

    return asset;
}

} // namespace assets