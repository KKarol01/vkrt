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

Geometry& Asset::get_geometry(const Submesh& submesh) { return geometries.at(submesh.geometry); }

Geometry* Asset::try_get_geometry(const Submesh& submesh) {
    if(submesh.geometry == s_max_asset_index) { return nullptr; }
    return &get_geometry(submesh);
}

Node& Asset::get_node(asset_index_t idx) { return nodes.at(idx); }

const Node& Asset::get_node(asset_index_t idx) const { return nodes.at(idx); }

glm::mat4& Asset::get_transform(Node& node) { return transforms.at(std::distance(nodes.data(), &node)); }

const glm::mat4& Asset::get_transform(const Node& node) const {
    return transforms.at(std::distance(nodes.data(), &node));
}

Mesh& Asset::get_mesh(const Node& node) { return meshes.at(node.mesh); }

Mesh* Asset::try_get_mesh(const Node& node) {
    if(node.mesh == s_max_asset_index) { return nullptr; }
    return &get_mesh(node);
}

Submesh& Asset::get_submesh(asset_index_t idx) { return submeshes.at(idx); }

Image& Asset::get_image(asset_index_t idx) { return images.at(idx); }

Texture& Asset::get_texture(asset_index_t idx) { return textures.at(idx); }

Material& Asset::get_material(asset_index_t idx) { return materials.at(idx); }

Material& Asset::get_material(const Submesh& submesh) { return materials.at(submesh.material); }

Material* Asset::try_get_material(const Submesh& submesh) {
    if(submesh.material == s_max_asset_index) { return nullptr; }
    return &get_material(submesh);
}

asset_index_t Asset::make_geometry() {
    geometries.emplace_back();
    return geometries.size() - 1;
}

asset_index_t Asset::make_node() {
    nodes.emplace_back();
    return nodes.size() - 1;
}

asset_index_t Asset::make_mesh() {
    meshes.emplace_back();
    return meshes.size() - 1;
}

asset_index_t Asset::make_submesh() {
    submeshes.emplace_back();
    return submeshes.size() - 1;
}

asset_index_t Asset::make_transform() {
    transforms.emplace_back();
    return transforms.size() - 1;
}

asset_index_t Asset::make_texture() {
    textures.emplace_back();
    return textures.size() - 1;
}

asset_index_t Asset::make_material() {
    materials.emplace_back();
    return materials.size() - 1;
}

Asset Importer::import_glb(const std::filesystem::path& path, ImportSettings settings) {
    Asset asset;
    fastgltf::Parser parser;
    auto glbbuffer = fastgltf::GltfDataBuffer::FromPath(path);
    if(!glbbuffer) {
        ENG_WARN("Error during fastgltf::GltfDataBuffer import: {}", fastgltf::getErrorName(glbbuffer.error()));
        return asset;
    }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    auto _gltfasset = parser.loadGltfBinary(glbbuffer.get(), path.parent_path(), gltfOptions);
    if(!_gltfasset) {
        ENG_WARN("Error during loading fastgltf::Parser::loadGltfBinary: {}", fastgltf::getErrorName(_gltfasset.error()));
        return asset;
    }
    auto& fasset = _gltfasset.get();
    auto& scene = fasset.scenes.at(0);

    asset.path = path;
    asset.nodes.resize(fasset.nodes.size());
    asset.transforms.resize(fasset.nodes.size());
    asset.images.reserve(fasset.images.size());
    asset.textures.reserve(fasset.textures.size());
    asset.meshes.reserve(fasset.meshes.size());
    asset.materials.reserve(fasset.materials.size());

    for(auto i = 0u; i < fasset.images.size(); ++i) {
        using namespace fastgltf;

        Image img;
        auto& fimg = fasset.images.at(i);
        std::span<const std::byte> data;
        if(auto fimgbview = std::get_if<sources::BufferView>(&fimg.data)) {
            auto& bview = fasset.bufferViews.at(fimgbview->bufferViewIndex);
            auto& buff = fasset.buffers.at(bview.bufferIndex);
            if(auto fimgarr = std::get_if<sources::Array>(&buff.data)) {
                data = { fimgarr->bytes.data() + bview.byteOffset, bview.byteLength };
            }
        }
        if(!data.empty()) {
            img.name = fimg.name.c_str();
            int x, y, ch;
            std::byte* imgdata =
                reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                                   data.size(), &x, &y, &ch, 4));
            if(!imgdata) {
                ENG_ERROR("Stbi failed: {}", stbi_failure_reason());
            } else {
                img.data = { imgdata, imgdata + x * y * ch };
                img.width = (uint32_t)x;
                img.height = (uint32_t)y;
                stbi_image_free(imgdata);
            }
        } else {
            ENG_WARN("Could not load image {}", fimg.name.c_str());
        }
        asset.images.push_back(std::move(img));
    }

    for(auto i = 0u; i < fasset.textures.size(); ++i) {
        auto& ftxt = fasset.textures.at(i);
        if(!ftxt.imageIndex) {
            ENG_WARN("Unsupported texture {} type.", ftxt.name.c_str());
            asset.make_texture();
        } else {
            auto& txt = asset.get_texture(asset.make_texture());
            if(ftxt.samplerIndex) {
                auto& fsamp = fasset.samplers.at(*ftxt.samplerIndex);
                ENG_TODO("Implement sampler settings import from fastgltf");
            }
            txt.image = *ftxt.imageIndex;
        }
    }

    for(auto i = 0u; i < fasset.materials.size(); ++i) {
        auto& fmat = fasset.materials.at(i);
        Material mat;
        mat.name = fmat.name.c_str();
        if(fmat.pbrData.baseColorTexture) {
            assert(fmat.pbrData.baseColorTexture->texCoordIndex == 0);
            auto& img = asset.get_image(asset.get_texture(fmat.pbrData.baseColorTexture->textureIndex).image);
            img.format = gfx::ImageFormat::R8G8B8A8_SRGB;
            mat.color_texture = fmat.pbrData.baseColorTexture->textureIndex;
        }
        asset.get_material(asset.make_material()) = std::move(mat);
    }

    for(auto i = 0u; i < fasset.meshes.size(); ++i) {
        auto& fmesh = fasset.meshes.at(i);
        Mesh mesh;
        mesh.name = fmesh.name.c_str();
        mesh.submeshes.reserve(fmesh.primitives.size());
        for(auto j = 0u; j < fmesh.primitives.size(); ++j) {
            const auto load_primitive = [&]() {
                Submesh submesh;
                auto& fprim = fmesh.primitives.at(j);
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;
                if(auto it = fprim.findAttribute("POSITION"); it != fprim.attributes.end()) {
                    auto& acc = fasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex) {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    vertices.resize(acc.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).position = { vec.x(), vec.y(), vec.z() };
                    });
                } else {
                    ENG_WARN("Mesh primitive does not contain position. Skipping...");
                    return submesh;
                }
                if(auto it = fprim.findAttribute("NORMAL"); it != fprim.attributes.end()) {
                    auto& acc = fasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex) {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).normal = { vec.x(), vec.y(), vec.z() };
                    });
                }
                if(auto it = fprim.findAttribute("TEXCOORD_0"); it != fprim.attributes.end()) {
                    auto& acc = fasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex) {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(fasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).uv = { vec.x(), vec.y() };
                    });
                }
                if(auto it = fprim.findAttribute("TANGENT"); it != fprim.attributes.end()) {
                    auto& acc = fasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex) {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(fasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).tangent = { vec.x(), vec.y(), vec.z(), vec.w() };
                    });
                }
                if(!fprim.indicesAccessor) {
                    ENG_WARN("Mesh primitive {}:{} does not have mandatory vertex indices. Skipping...", fmesh.name.c_str(), j);
                    return submesh;
                } else {
                    auto& acc = fasset.accessors.at(*fprim.indicesAccessor);
                    if(!acc.bufferViewIndex) {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    indices.resize(acc.count);
                    fastgltf::copyFromAccessor<uint32_t>(fasset, acc, indices.data());
                }
                if(fprim.materialIndex) {
                    submesh.material = *fprim.materialIndex;
                } else {
                    ENG_WARN("Mesh prim {}:{} does not have a material", fmesh.name.c_str(), j);
                }
                submesh.geometry = asset.make_geometry();
                auto& geom = asset.get_geometry(submesh);
                geom.vertex_range = { .offset = asset.vertices.size(), .count = vertices.size() };
                geom.index_range = { .offset = asset.indices.size(), .count = indices.size() };
                asset.vertices.insert(asset.vertices.end(), vertices.begin(), vertices.end());
                asset.indices.insert(asset.indices.end(), indices.begin(), indices.end());
                return submesh;
            };

            const auto submeshidx = asset.make_submesh();
            asset.get_submesh(submeshidx) = load_primitive();
            mesh.submeshes.push_back(submeshidx);
        }
        asset.meshes.push_back(mesh);
    }

    fastgltf::iterateSceneNodes(fasset, 0ull, fastgltf::math::fmat4x4{},
                                [&](fastgltf::Node& fnode, const fastgltf::math::fmat4x4& mat) {
                                    const auto fidx = std::distance(fasset.nodes.data(), &fnode);
                                    auto& node = asset.get_node(fidx);
                                    auto& trans = asset.get_transform(node);
                                    memcpy(&trans, &mat, sizeof(trans));
                                    node.name = fnode.name.c_str();
                                    if(fnode.meshIndex) { node.mesh = *fnode.meshIndex; }
                                    node.nodes.reserve(fnode.children.size());
                                    for(auto i : fnode.children) {
                                        node.nodes.push_back(i);
                                    }
                                });
    asset.scene.reserve(scene.nodeIndices.size());
    for(auto sidx : scene.nodeIndices) {
        asset.scene.push_back(sidx);
    }

    return asset;
}

} // namespace assets