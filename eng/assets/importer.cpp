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

Node& Asset::get_node(asset_index_t idx) { return nodes.at(idx); }

const Node& assets::Asset::get_node(asset_index_t idx) const { return nodes.at(idx); }

glm::mat4 assets::Asset::get_transform(const Node& node) const { return transforms.at(node.transform); }

asset_index_t Asset::make_node() {
    nodes.emplace_back();
    return nodes.size();
}

asset_index_t Asset::make_mesh() {
    meshes.emplace_back();
    return meshes.size();
}

asset_index_t Asset::make_submesh() {
    submeshes.emplace_back();
    return submeshes.size();
}

glm::mat4& Asset::make_transform() { return transforms.emplace_back(); }

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
    auto& fasset = _gltfasset.get();
    auto& scene = fasset.scenes.at(0);

    asset.path = full_path;
    asset.nodes.reserve(fasset.nodes.size());
    asset.transforms.reserve(asset.nodes.capacity());
    asset.images.reserve(fasset.images.size());
    asset.textures.reserve(fasset.textures.size());
    asset.meshes.reserve(fasset.meshes.size());
    asset.materials.reserve(fasset.materials.size());

    const auto load_image = [&fasset, &asset](uint32_t index, gfx::ImageFormat format) {
        auto& img = fasset.images.at(index);
        if(!std::holds_alternative<fastgltf::sources::BufferView>(img.data)) {
            ENG_ERROR("Image {} not loaded. Invalid image data source.", img.name);
            return;
        }
        auto& bview = fasset.bufferViews.at(std::get<fastgltf::sources::BufferView>(img.data).bufferViewIndex);
        auto& buff = fasset.buffers.at(bview.bufferIndex);
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

    std::unordered_map<const fastgltf::Node*, asset_index_t> fnode_idxs;
    const auto get_node_idx = [&fnode_idxs, &asset](const fastgltf::Node& node) {
        if(auto it = fnode_idxs.find(&node); it != fnode_idxs.end()) { return it->second; }
        const auto idx = static_cast<asset_index_t>(asset.nodes.size());
        asset.make_node();
        fnode_idxs[&node] = idx;
        return idx;
    };
    const auto parseFMesh = [&fasset](Mesh& m, const fastgltf::Mesh& fm) {
        static const std::array<std::string, 4> attribs{ "POSITION", "NORMAL", "TEXCOORD_0", "TANGENT" };
        static const std::array<uint32_t, 4> attrib_offsets{ offsetof(gfx::Vertex, pos), offsetof(gfx::Vertex, nor),
                                                             offsetof(gfx::Vertex, uv), offsetof(gfx::Vertex, tang) };
        static const std::array<uint32_t, 4> attrib_sizes{ sizeof(gfx::Vertex::pos), sizeof(gfx::Vertex::nor),
                                                           sizeof(gfx::Vertex::uv), sizeof(gfx::Vertex::tang) };

        for(auto& p : fm.primitives) {
            for(auto i = 0u; i < attribs.size(); ++i) {
                auto attr = p.findAttribute(attribs.at(i));
                if(i == 0u && attr == p.attributes.end()) { break; }
                fastgltf::iterateAccessor()
            }
        }
    };
    const auto iterateSceneNodes = [&asset, &fasset, &fnode_idxs, &get_node_idx,
                                    &parseFMesh](fastgltf::Node& node, const fastgltf::math::fmat4x4& mat) {
        const auto nidx = get_node_idx(node);
        auto& n = asset.get_node(nidx);
        n.name = node.name.c_str();
        n.nodes.reserve(node.children.size());
        for(auto nidx : node.children) {
            n.nodes.push_back(get_node_idx(fasset.nodes.at(nidx)));
        }
        n.transform = asset.transforms.size();
        auto& t = asset.make_transform();
        memcpy(&t, &mat, sizeof(t));
        if(node.meshIndex) {
            n.meshes.push_back(asset.make_mesh());
            parseFMesh(asset.meshes.back(), fasset.meshes.at(*node.meshIndex));
        }
    };

    fastgltf::iterateSceneNodes(fasset, 0ull, fastgltf::math::fmat4x4{}, iterateSceneNodes);
    asset.scene.reserve(scene.nodeIndices.size());
    for(auto sidx : scene.nodeIndices) {
        asset.scene.push_back(get_node_idx(fasset.nodes.at(sidx)));
    }

    return asset;
}

} // namespace assets