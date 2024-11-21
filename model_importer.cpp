#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <stb/stb_image.h>
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "model_importer.hpp"
#include "set_debug_name.hpp"

static void load_image_load_buffer_view(ImportedModel& model, const fastgltf::Asset& asset,
                                        const fastgltf::Image& image, const fastgltf::sources::BufferView& source) {
    auto& view = asset.bufferViews.at(source.bufferViewIndex);
    auto& buffer = asset.buffers.at(view.bufferIndex);

    const uint32_t channels = 4;
    uint32_t width = 0, height = 0;
    std::byte* loaded_image = nullptr;

    std::visit(fastgltf::visitor{
                   [](auto& arg) { throw std::runtime_error{ "Fastgltf Buffer source type not supported" }; },
                   [&](const fastgltf::sources::Array& source) {
                       int w, h, ch;
                       uint8_t* d = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(source.bytes.data() + view.byteOffset),
                                                          view.byteLength, &w, &h, &ch, 4);
                       width = std::max(0, w);
                       height = std::max(0, h);
                       loaded_image = reinterpret_cast<std::byte*>(d);
                   } },
               buffer.data);

    model.textures.push_back(ImportedModel::Texture{ .name = image.name.c_str(),
                                                     .size = { width, height },
                                                     .rgba_data = { loaded_image, loaded_image + width * height * channels } });
    stbi_image_free(loaded_image);
}

static void load_image(ImportedModel& model, fastgltf::Asset& asset, fastgltf::Image& image) {
    std::visit(fastgltf::visitor{
                   [](auto& arg) { throw std::runtime_error{ "Fastgltf Image source type not supported" }; },
                   [&](fastgltf::sources::BufferView& source) {
                       load_image_load_buffer_view(model, asset, image, source);
                   },
               },
               image.data);
}

static ImportedModel::Material& get_or_create_material(ImportedModel& model, uint32_t index) {
    if(model.materials.size() <= index) { model.materials.resize(index + 1); }
    return model.materials.at(index);
}

static void load_mesh(ImportedModel& model, fastgltf::Asset& asset, fastgltf::Mesh& mesh) {
    for(uint32_t i = 0; i < mesh.primitives.size(); ++i) {
        auto& prim = mesh.primitives.at(i);
        ImportedModel::Mesh imesh;
        imesh.name = mesh.name;

        auto itp = prim.findAttribute("POSITION");
        const auto num_positions = asset.accessors.at(itp->accessorIndex).count;

        if(num_positions == 0) { continue; }

        imesh.vertex_offset = model.vertices.size();
        imesh.index_offset = model.indices.size();
        imesh.vertex_count = num_positions;

        model.vertices.resize(model.vertices.size() + num_positions);

        const auto push_vertex_attrib = [&]<typename GLM>(auto&& it, uint64_t offset) {
            auto& acc = asset.accessors.at(it->accessorIndex);
            auto& index = acc.bufferViewIndex;
            const auto num_comp = fastgltf::getNumComponents(acc.type);

            if(index) {
                if(num_comp == 2) {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(
                        asset, asset.accessors.at(it->accessorIndex), [&](const glm::vec2& p, uint64_t idx) {
                            memcpy(reinterpret_cast<std::byte*>(&model.vertices.at(imesh.vertex_offset + idx)) + offset,
                                   &p, sizeof(GLM));
                        });
                } else if(num_comp == 3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset, asset.accessors.at(it->accessorIndex), [&](const glm::vec3& p, uint64_t idx) {
                            memcpy(reinterpret_cast<std::byte*>(&model.vertices.at(imesh.vertex_offset + idx)) + offset,
                                   &p, sizeof(GLM));
                        });
                } else if(num_comp == 4) {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset, asset.accessors.at(it->accessorIndex), [&](const glm::vec4& p, uint64_t idx) {
                            memcpy(reinterpret_cast<std::byte*>(&model.vertices.at(imesh.vertex_offset + idx)) + offset,
                                   &p, sizeof(GLM));
                        });
                }
            }
        };

        push_vertex_attrib.operator()<glm::vec3>(itp, offsetof(ImportedModel::Vertex, pos));

        if(auto it = prim.findAttribute("NORMAL"); it) {
            push_vertex_attrib.operator()<glm::vec3>(prim.findAttribute("NORMAL"), offsetof(ImportedModel::Vertex, nor));
        }

        if(auto it = prim.findAttribute("TEXCOORD_0"); it) {
            push_vertex_attrib.operator()<glm::vec2>(prim.findAttribute("TEXCOORD_0"), offsetof(ImportedModel::Vertex, uv));
        }

        if(auto it = prim.findAttribute("TANGENT"); it) {
            push_vertex_attrib.operator()<glm::vec4>(prim.findAttribute("TANGENT"), offsetof(ImportedModel::Vertex, tang));
        }

        auto& indices = asset.accessors.at(prim.indicesAccessor.value());
        imesh.index_count = indices.count;

        model.indices.resize(model.indices.size() + indices.count);
        fastgltf::copyFromAccessor<uint32_t>(asset, indices, model.indices.data() + imesh.index_offset);

        if(prim.materialIndex) {
            auto& mat = asset.materials.at(*prim.materialIndex);
            auto& imat = get_or_create_material(model, *prim.materialIndex);
            imesh.material = *prim.materialIndex;

            imat.name = mat.name;

            if(mat.pbrData.baseColorTexture) {
                imat.color_texture = *asset.textures.at(mat.pbrData.baseColorTexture->textureIndex).imageIndex;
            }
            if(mat.normalTexture) {
                imat.normal_texture = *asset.textures.at(mat.normalTexture->textureIndex).imageIndex;
            }
            if(mat.pbrData.metallicRoughnessTexture) {
                auto& mr_tex = asset.textures.at(mat.pbrData.metallicRoughnessTexture->textureIndex);
                imat.metallic_roughness_texture = *mr_tex.imageIndex;
            }
        }

        model.meshes.push_back(std::move(imesh));
    }
}

ImportedModel ModelImporter::import_model(const std::filesystem::path& path) {
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                 fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;

    const std::filesystem::path full_path = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "models" / path;

    fastgltf::Parser parser;
    auto glbbuffer = fastgltf::GltfDataBuffer::FromPath(full_path);
    const auto glb_error = glbbuffer.error();

    auto asset = parser.loadGltfBinary(glbbuffer.get(), full_path.parent_path(), gltfOptions);

    ImportedModel model;

    for(auto& img : asset->images) {
        load_image(model, asset.get(), img);
    }

    for(auto& mesh : asset->meshes) {
        load_mesh(model, asset.get(), mesh);
    }

    return model;
}
