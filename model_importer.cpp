#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <stb/stb_image.h>
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "model_importer.hpp"
#include "set_debug_name.hpp"

static void load_image_load_buffer_view(ImportedModel& model, const fastgltf::Asset& asset, const fastgltf::Image& image,
                                        const fastgltf::sources::BufferView& source) {
    auto& view = asset.bufferViews.at(source.bufferViewIndex);
    auto& buffer = asset.buffers.at(view.bufferIndex);

    const uint32_t channels = 4;
    uint32_t width = 0, height = 0;
    std::byte* loaded_image = nullptr;

    std::visit(fastgltf::visitor{ [](auto& arg) { throw std::runtime_error{ "Fastgltf Buffer source type not supported" }; },
                                  [&](const fastgltf::sources::Array& source) {
                                      int w, h, ch;
                                      uint8_t* d = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(source.bytes.data() + view.byteOffset),
                                                                         view.byteLength, &w, &h, &ch, 4);
                                      width = std::max(0, w);
                                      height = std::max(0, h);
                                      loaded_image = reinterpret_cast<std::byte*>(d);
                                  } },
               buffer.data);

    model.textures.push_back(ImportedModel::Texture{ .name = image.name.c_str(), .rgba_data = { loaded_image, loaded_image + width * height * channels } });
    stbi_image_free(loaded_image);
}

static void load_image(ImportedModel& model, fastgltf::Asset& asset, fastgltf::Image& image) {
    std::visit(fastgltf::visitor{
                   [](auto& arg) { throw std::runtime_error{ "Fastgltf Image source type not supported" }; },
                   [&](fastgltf::sources::BufferView& source) { load_image_load_buffer_view(model, asset, image, source); },
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

            if(index) {
                fastgltf::iterateAccessorWithIndex<GLM>(asset, asset.accessors.at(it->accessorIndex), [&](const GLM& p, uint64_t idx) {
                    memcpy(reinterpret_cast<std::byte*>(&model.vertices.at(imesh.vertex_offset + idx)) + offset, &p, sizeof(GLM));
                });
            }
        };

        push_vertex_attrib.operator()<glm::vec3>(itp, offsetof(ImportedModel::Vertex, pos));

        if(auto it = prim.findAttribute("NORMAL"); it) {
            push_vertex_attrib.operator()<glm::vec3>(prim.findAttribute("NORMAL"), offsetof(ImportedModel::Vertex, nor));
        }

        if(auto it = prim.findAttribute("TEXCOORD_0"); it) {
            push_vertex_attrib.operator()<glm::vec2>(prim.findAttribute("TEXCOORD_0"), offsetof(ImportedModel::Vertex, uv));
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
                auto& base_tex = asset.textures.at(mat.pbrData.baseColorTexture->textureIndex);
                imat.color_texture = *base_tex.imageIndex;
            }
        }

        model.meshes.push_back(std::move(imesh));
    }
}

// ModelTexture::ModelTexture(const std::string& name, int w, int h, int ch, uint8_t* data) : name(name) {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//
//     vks::ImageCreateInfo info;
//     info.imageType = VK_IMAGE_TYPE_2D;
//     info.mipLevels = 1 + std::log2(w < h ? w : h);
//     info.arrayLayers = 1;
//     info.extent = VkExtent3D{ (uint32_t)w, (uint32_t)h, 1 };
//     info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//     info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
//     info.tiling = VK_IMAGE_TILING_OPTIMAL;
//     info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//     info.format = VK_FORMAT_R8G8B8A8_SRGB;
//     info.samples = VK_SAMPLE_COUNT_1_BIT;
//
//     vks::BufferCreateInfo buffer_info;
//     buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
//     buffer_info.size = w * h * ch;
//
//     VmaAllocationCreateInfo alloc_create_info{
//         .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
//         .usage = VMA_MEMORY_USAGE_AUTO,
//     };
//
//     VkBuffer staging_buffer;
//     VmaAllocation buffer_alloc;
//     VmaAllocationInfo buffer_alloc_info;
//     if(vmaCreateBuffer(renderer->vma, &buffer_info, &alloc_create_info, &staging_buffer, &buffer_alloc, &buffer_alloc_info) != VK_SUCCESS) {
//         throw std::runtime_error{ "Could not create staging buffer" };
//     }
//
//     alloc_create_info = { .flags = {}, .usage = VMA_MEMORY_USAGE_AUTO };
//     if(vmaCreateImage(renderer->vma, &info, &alloc_create_info, &image, &alloc, &alloc_info) != VK_SUCCESS) {
//         throw std::runtime_error{ "ModelTexture Image could not be allocated" };
//     }
//
//     vks::ImageViewCreateInfo view_info;
//     view_info.image = image;
//     view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
//     view_info.components = {};
//     view_info.subresourceRange = {
//         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
//         .baseMipLevel = 0,
//         .levelCount = info.mipLevels,
//         .baseArrayLayer = 0,
//         .layerCount = 1,
//     };
//     view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
//
//     if(vkCreateImageView(renderer->dev, &view_info, nullptr, &view) != VK_SUCCESS) {
//         throw std::runtime_error{ "ModelTexture View could not be allocated" };
//     }
//
//     memcpy(buffer_alloc_info.pMappedData, data, w * h * ch);
//
//     vks::BufferImageCopy2 image_copy;
//     image_copy.bufferOffset = 0;
//     image_copy.bufferImageHeight = 0;
//     image_copy.bufferRowLength = 0;
//     image_copy.imageOffset = VkOffset3D{};
//     image_copy.imageExtent = VkExtent3D{ (uint32_t)w, (uint32_t)h, 1 };
//     image_copy.imageSubresource = {
//         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
//         .mipLevel = 0,
//         .baseArrayLayer = 0,
//         .layerCount = 1,
//     };
//
//     vks::CopyBufferToImageInfo2 buffer_copy_info;
//     buffer_copy_info.srcBuffer = staging_buffer;
//     buffer_copy_info.dstImage = image;
//     buffer_copy_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//     buffer_copy_info.pRegions = &image_copy;
//     buffer_copy_info.regionCount = 1;
//
//     static constexpr auto to_layout = [&](VkCommandBuffer cmd, VkImage image, uint32_t mip, VkImageLayout old_layout,
//                                           VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage,
//                                           VkAccessFlags2 src_access, VkAccessFlags2 dst_access) {
//         vks::ImageMemoryBarrier2 imgb;
//         imgb.image = image;
//         imgb.oldLayout = old_layout;
//         imgb.newLayout = new_layout;
//         imgb.srcStageMask = src_stage;
//         imgb.dstStageMask = dst_stage;
//         imgb.srcAccessMask = src_access;
//         imgb.dstAccessMask = dst_access;
//         imgb.subresourceRange = {
//             .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
//             .baseMipLevel = mip,
//             .levelCount = 1,
//             .baseArrayLayer = 0,
//             .layerCount = 1,
//         };
//
//         vks::DependencyInfo dep;
//         dep.pImageMemoryBarriers = &imgb;
//         dep.imageMemoryBarrierCount = 1;
//
//         vkCmdPipelineBarrier2(cmd, &dep);
//     };
//
//     vks::CommandBufferBeginInfo cmdbi;
//     cmdbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//
//     vkBeginCommandBuffer(renderer->cmd, &cmdbi);
//     to_layout(renderer->cmd, image, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE,
//               VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT);
//     vkCmdCopyBufferToImage2(renderer->cmd, &buffer_copy_info);
//
//     for(uint32_t i = 1; i < info.mipLevels; ++i) {
//         to_layout(renderer->cmd, image, i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
//                   VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
//         to_layout(renderer->cmd, image, i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE,
//                   VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT);
//
//         vks::ImageBlit2 region;
//         region.srcOffsets[0] = {};
//         region.srcOffsets[1] = { w >> (i - 1), h >> (i - 1), 1 };
//         region.dstOffsets[0] = {};
//         region.dstOffsets[1] = { w >> i, h >> i, 1 };
//         region.srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = i - 1, .baseArrayLayer = 0, .layerCount = 1 };
//         region.dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = i, .baseArrayLayer = 0, .layerCount = 1 };
//
//         vks::BlitImageInfo2 blit;
//         blit.srcImage = image;
//         blit.dstImage = image;
//         blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
//         blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//         blit.filter = VK_FILTER_LINEAR;
//         blit.pRegions = &region;
//         blit.regionCount = 1;
//         vkCmdBlitImage2(renderer->cmd, &blit);
//
//         to_layout(renderer->cmd, image, i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
//                   VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_SHADER_READ_BIT);
//     }
//     if(info.mipLevels > 1) {
//         to_layout(renderer->cmd, image, info.mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
//                   VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_SHADER_READ_BIT);
//     }
//
//     vkEndCommandBuffer(renderer->cmd);
//
//     vks::CommandBufferSubmitInfo cmd_submit_info;
//     cmd_submit_info.commandBuffer = renderer->cmd;
//
//     vks::SubmitInfo2 submit_info;
//     submit_info.pCommandBufferInfos = &cmd_submit_info;
//     submit_info.commandBufferInfoCount = 1;
//     vkQueueSubmit2(renderer->gq, 1, &submit_info, nullptr);
//
//     set_debug_name(image, name);
//     set_debug_name(view, name + "_default_view");
//
//     vkDeviceWaitIdle(renderer->dev);
//
//     vmaDestroyBuffer(renderer->vma, staging_buffer, buffer_alloc);
// }

ImportedModel ModelImporter::import_model(const std::string& name, const std::filesystem::path& path) {
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                 fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;

    const std::filesystem::path full_path = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "models" / path;

    fastgltf::Parser parser;
    auto glbbuffer = fastgltf::GltfDataBuffer::FromPath(full_path);
    const auto glb_error = glbbuffer.error();

    auto asset = parser.loadGltfBinary(glbbuffer.get(), full_path.parent_path(), gltfOptions);

    ImportedModel model;
    model.name = name;

    for(auto& img : asset->images) {
        load_image(model, asset.get(), img);
    }

    for(auto& mesh : asset->meshes) {
        load_mesh(model, asset.get(), mesh);
    }

    return model;
}
