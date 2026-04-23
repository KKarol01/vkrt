#include "loaders.hpp"

#include <variant>
#include <vector>
#include <cstddef>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <eng/ecs/components.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>

namespace eng
{
namespace assets
{

namespace gltf
{

struct Context
{
    Flags<ImportSettings> import_settings;
    std::vector<uint32_t> images;
    std::vector<uint32_t> textures;
    std::vector<Range32u> materials;
    std::vector<Range32u> geometries;
    std::vector<Range32u> meshes;
};

Range32u load_geometry(Asset& asset, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Context& ctx)
{
    if(ctx.geometries.empty()) { ctx.geometries.insert(ctx.geometries.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.geometries[gltfmeshidx].size != 0u) { return ctx.geometries[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];

    Range32u geoms{ (uint32_t)asset.geometries.size(), 0u };
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    for(auto i = 0ull; i < gltfmesh.primitives.size(); ++i)
    {
        vertices.clear();
        indices.clear();
        const fastgltf::Primitive& gltfprim = gltfmesh.primitives[i];

        static constexpr const char* FAST_COMPS[]{ "POSITION", "NORMAL", "TANGENT", "TEXCOORD_0" };
        static constexpr gfx::VertexComponent GFX_COMPS[]{ gfx::VertexComponent::POSITION_BIT, gfx::VertexComponent::NORMAL_BIT,
                                                           gfx::VertexComponent::TANGENT_BIT, gfx::VertexComponent::UV0_BIT };
        const auto vertex_layout = [&gltfprim] {
            Flags<gfx::VertexComponent> vertex_layout{};
            for(auto i = 0u; i < std::size(FAST_COMPS); ++i)
            {
                if(gltfprim.findAttribute(FAST_COMPS[i]) != gltfprim.attributes.end())
                {
                    vertex_layout |= GFX_COMPS[i];
                }
            }
            return vertex_layout;
        }();
        const auto vertex_size = gfx::get_vertex_layout_size(vertex_layout);
        const auto get_vertex_component = [&vertices, &vertex_size,
                                           &vertex_layout](size_t vidx, gfx::VertexComponent comp) -> std::byte* {
            auto* ptr = (std::byte*)vertices.data();
            return ptr + vertex_size * vidx + gfx::get_vertex_component_offset(vertex_layout, comp);
        };

        const auto fast_iterate = [&gltfasset, &get_vertex_component](int comp, const auto& gltfacc, gfx::VertexComponent gfxcomp) {
            const auto copy_bytes = [&get_vertex_component, &gfxcomp]<size_t num_floats>(const auto& vec, auto idx) {
                std::array<float, num_floats> v{};
                for(auto i = 0u; i < num_floats; ++i)
                {
                    v[i] = (float)vec[i];
                }
                auto* pdst = get_vertex_component(idx, gfxcomp);
                memcpy(pdst, v.data(), num_floats * sizeof(v[0]));
            };
            const auto cb2 = [&copy_bytes](const auto& vec, auto idx) { copy_bytes.template operator()<2>(vec, idx); };
            const auto cb3 = [&copy_bytes](const auto& vec, auto idx) { copy_bytes.template operator()<3>(vec, idx); };
            const auto cb4 = [&copy_bytes](const auto& vec, auto idx) { copy_bytes.template operator()<4>(vec, idx); };

            if(comp == 0) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltfasset, gltfacc, cb3); }
            else if(comp == 1) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltfasset, gltfacc, cb3); }
            else if(comp == 2) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltfasset, gltfacc, cb4); }
            else if(comp == 3) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(gltfasset, gltfacc, cb2); }
        };

        for(auto i = 0u; i < std::size(FAST_COMPS); ++i)
        {
            if(!vertex_layout.test(GFX_COMPS[i]))
            {
                if(i == 0)
                {
                    ENG_ERROR("Mesh does not have positions");
                    continue;
                }
                continue;
            }
            auto it = gltfprim.findAttribute(FAST_COMPS[i]);
            auto& acc = gltfasset.accessors.at(it->accessorIndex);
            if(i == 0) { vertices.resize(acc.count * vertex_size / sizeof(float)); }
            fast_iterate(i, acc, GFX_COMPS[i]);
        }

        if(gltfprim.indicesAccessor)
        {
            auto& acc = gltfasset.accessors.at(*gltfprim.indicesAccessor);
            if(!acc.bufferViewIndex)
            {
                ENG_ERROR("No bufferViewIndex...");
                continue;
            }
            indices.resize(acc.count);
            fastgltf::copyFromAccessor<uint32_t>(gltfasset, acc, indices.data());
        }
        else
        {
            ENG_WARN("Mesh {} primitive {} does not have mandatory vertex indices. Skipping...", gltfmesh.name.c_str(), i);
            continue;
        }

        asset.geometries.push_back(get_engine().renderer->make_geometry(gfx::GeometryDescriptor{
            .flags = {},
            .vertex_layout = vertex_layout,
            .index_format = gfx::IndexFormat::U32,
            .vertices = vertices,
            .indices = std::as_bytes(std::span{ indices }),
            .signal = ctx.import_settings.test(ImportSettings::KEEP_DATA_BIT)
                          ? &asset.geometry_data.emplace_back(gfx::ParsedGeometryReadySignal{})
                          : (gfx::ParsedGeometryReadySignal*)nullptr,
        }));
        if(ctx.import_settings & ImportSettings::KEEP_DATA_BIT)
        {
            asset.geometry_data_futures.push_back(asset.geometry_data.back().get_future().share());
        }
        ++geoms.size;
    }

    ctx.geometries[gltfmeshidx] = geoms;
    return geoms;
}

uint32_t load_image(Asset& asset, const fastgltf::Asset& gltfasset, gfx::ImageFormat format, size_t gltfimgidx, Context& ctx)
{
    // todo: check if image format matches with the currently requested.
    if(ctx.images.empty()) { ctx.images.insert(ctx.images.begin(), gltfasset.images.size(), ~0u); }
    if(ctx.images[gltfimgidx] != ~0u) { return ctx.images[gltfimgidx]; }

    const fastgltf::Image& gltfimg = gltfasset.images[gltfimgidx];

    std::span<const std::byte> data;
    if(auto* fastbufviewsrc = std::get_if<fastgltf::sources::BufferView>(&gltfimg.data))
    {
        auto& fastbufview = gltfasset.bufferViews.at(fastbufviewsrc->bufferViewIndex);
        auto& fastbuf = gltfasset.buffers.at(fastbufview.bufferIndex);
        if(auto* fastarrsrc = std::get_if<fastgltf::sources::Array>(&fastbuf.data))
        {
            data = { fastarrsrc->bytes.data() + fastbufview.byteOffset, fastbufview.byteLength };
        }
    }
    else if(auto* fastarrsrc = std::get_if<fastgltf::sources::Array>(&gltfimg.data))
    {
        data = { fastarrsrc->bytes.begin(), fastarrsrc->bytes.end() };
    }

    if(data.empty())
    {
        ENG_WARN("Could not load image {}", gltfimg.name.c_str());
        return ~0u;
    }

    int x, y, ch;
    std::byte* imgdata = reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                                            data.size(), &x, &y, &ch, 4));

    if(!imgdata)
    {
        ENG_ERROR("Stbi failed for image {}: {}", gltfimg.name.c_str(), stbi_failure_reason());
        return ~0u;
    }

    const auto img = get_engine().renderer->make_image(gltfimg.name.c_str(),
                                                       gfx::Image::init((uint32_t)x, (uint32_t)y, 0, format,
                                                                        gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                            gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                        0, 1, gfx::ImageLayout::READ_ONLY));
    if(!img) { ENG_ERROR("Failed to create image{}", gltfimg.name.c_str()); }
    else
    {
        gfx::get_renderer().staging->copy(img.get(), imgdata, 0, 0, true, gfx::DiscardContents::YES);
        ENG_TODO("TODO: Process mips");
    }

    if(!img) { return ~0u; }

    const uint32_t imgidx = asset.images.size();
    asset.images.push_back(img);
    ctx.images[gltfimgidx] = imgidx;

    if(ctx.import_settings & ImportSettings::KEEP_DATA_BIT)
    {
        asset.image_data.emplace_back((uint32_t)x, (uint32_t)y, format, std::vector<std::byte>(imgdata, imgdata + x * y * ch));
    }
    stbi_image_free(imgdata);

    return imgidx;
}

uint32_t load_texture(Asset& asset, const fastgltf::Asset& gltfasset, gfx::ImageFormat format, size_t gltftexidx, Context& ctx)
{
    if(ctx.textures.empty()) { ctx.textures.insert(ctx.textures.begin(), gltfasset.textures.size(), ~0u); }
    if(ctx.textures[gltftexidx] != ~0u) { return ctx.textures[gltftexidx]; }
    const auto& gltftex = gltfasset.textures[gltftexidx];
    if(!gltftex.imageIndex)
    {
        ENG_ERROR("Texture {} does not have associated image", gltftex.name.c_str());
        return ~0u;
    }
    uint32_t image = load_image(asset, gltfasset, format, *gltftex.imageIndex, ctx);
    if(image == ~0u) { return ~0u; }

    uint32_t texidx = asset.textures.size();
    asset.textures.push_back(gfx::ImageView::init(asset.images[image], format));
    ctx.textures[gltftexidx] = texidx;
    return texidx;
}

Range32u load_material(Asset& asset, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Context& ctx)
{
    if(ctx.materials.empty()) { ctx.materials.insert(ctx.materials.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.materials[gltfmeshidx].size != 0u) { return ctx.materials[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];
    Range32u mats{ (uint32_t)asset.materials.size(), 0u };
    for(auto i = 0ull; i < gltfmesh.primitives.size(); ++i)
    {
        const fastgltf::Primitive& gltfprim = gltfmesh.primitives[i];

        if(!gltfprim.materialIndex)
        {
            ENG_ERROR("Primitive {} in mesh {} does not have a material", gltfmesh.name.c_str(), i);
            continue;
        }

        const fastgltf::Material& gltfmat = gltfasset.materials[*gltfprim.materialIndex];
        auto mat = gfx::Material::init(gltfmat.name.c_str());
        if(gltfmat.pbrData.baseColorTexture)
        {
            uint32_t texidx = load_texture(asset, gltfasset, gfx::ImageFormat::R8G8B8A8_SRGB,
                                           gltfmat.pbrData.baseColorTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                mat.base_color_texture = asset.textures[ctx.textures[gltfmat.pbrData.baseColorTexture->textureIndex]];
            }
        }
        if(gltfmat.normalTexture)
        {
            uint32_t texidx =
                load_texture(asset, gltfasset, gfx::ImageFormat::R8G8B8A8_UNORM, gltfmat.normalTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                mat.normal_texture = asset.textures[ctx.textures[gltfmat.normalTexture->textureIndex]];
            }
        }
        if(gltfmat.pbrData.metallicRoughnessTexture)
        {
            uint32_t texidx = load_texture(asset, gltfasset, gfx::ImageFormat::R8G8B8A8_UNORM,
                                           gltfmat.pbrData.metallicRoughnessTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                mat.metallic_roughness_texture =
                    asset.textures[ctx.textures[gltfmat.pbrData.metallicRoughnessTexture->textureIndex]];
            }
        }

        asset.materials.push_back(get_engine().renderer->make_material(mat));
        ++mats.size;
    }

    ctx.materials[gltfmeshidx] = mats;
    return mats;
}

Range32u load_mesh(Asset& asset, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Asset::Node& node, Context& ctx)
{
    if(ctx.meshes.empty()) { ctx.meshes.insert(ctx.meshes.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.meshes[gltfmeshidx].size != 0u) { return ctx.meshes[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];

    ENG_TIMER_START("Load geometry {}", node.name);
    Range32u geoms = load_geometry(asset, gltfasset, gltfmeshidx, ctx);
    ENG_TIMER_END();
    ENG_TIMER_START("Load material {}", node.name);
    Range32u mats = load_material(asset, gltfasset, gltfmeshidx, ctx);
    ENG_TIMER_END();
    ENG_ASSERT(geoms == mats && geoms.offset == (uint32_t)asset.meshes.size());
    for(auto i = 0u; i < geoms.size; ++i)
    {
        asset.meshes.push_back(gfx::get_renderer().make_mesh(gfx::MeshDescriptor{
            .geometry = asset.geometries[geoms.offset + i], .material = asset.materials[mats.offset + i] }));
    }
    ctx.meshes[gltfmeshidx] = geoms;
    return geoms;
}

Asset::Node load_node(Asset& asset, const fastgltf::Asset& gltfasset, const fastgltf::Node& gltfnode,
                      Asset::Node* parent_node, Context& context, uint32_t* out_node_index)
{
    Asset::Node node{};
    node.name = gltfnode.name.c_str();
    node.transform = [&] {
        auto& transform = asset.transforms.emplace_back();
        const auto parent_transform =
            parent_node ? asset.transforms[parent_node->transform].to_mat4() : glm::identity<glm::mat4>();
        if(gltfnode.transform.index() == 0)
        {
            const auto& gltftrs = std::get<fastgltf::TRS>(gltfnode.transform);
            ecs::Transform trs{};
            trs.position = glm::vec3{ gltftrs.translation.x(), gltftrs.translation.y(), gltftrs.translation.z() };
            trs.rotation = glm::quat{ gltftrs.rotation.w(), gltftrs.rotation.x(), gltftrs.rotation.y(), gltftrs.rotation.z() };
            trs.scale = glm::vec3{ gltftrs.scale.x(), gltftrs.scale.y(), gltftrs.scale.z() };
            transform = ecs::Transform::init(parent_transform * trs.to_mat4());
        }
        else
        {
            const auto& fasttrs = std::get<fastgltf::math::fmat4x4>(gltfnode.transform);
            glm::mat4 trs;
            memcpy(&trs, &fasttrs, sizeof(fasttrs));
            transform = ecs::Transform::init(parent_transform * trs);
        }
        return asset.transforms.size() - 1;
    }();

    if(gltfnode.meshIndex)
    {
        node.meshes = load_mesh(asset, gltfasset, *gltfnode.meshIndex, node, context);
        if(node.meshes.size == 0u)
        {
            ENG_ERROR("Failed to load mesh {} for node {}.", gltfasset.meshes.at(*gltfnode.meshIndex).name.c_str(),
                      gltfnode.name.c_str());
        }
    }

    std::vector<Asset::Node> children;
    children.reserve(gltfnode.children.size());
    for(const auto& e : gltfnode.children)
    {
        uint32_t out_idx;
        children.push_back(load_node(asset, gltfasset, gltfasset.nodes[e], &node, context, &out_idx));
    }
    const auto nodeidx = (uint32_t)asset.nodes.size();
    node.children = { (uint32_t)asset.nodes.size() + 1, (uint32_t)children.size() };
    asset.nodes.push_back(node);
    for(auto& child : children)
    {
        child.parent = nodeidx;
        asset.nodes.push_back(child);
    }

    if(out_node_index) { *out_node_index = nodeidx; }
    return node;
}

} // namespace gltf

std::optional<Asset> AssetLoaderGLTF::load_from_file(const fs::Path& file_path, Flags<ImportSettings> import_settings)
{
    auto fastdatabuf = fastgltf::GltfDataBuffer::FromPath(file_path);
    if(!fastdatabuf) { return std::nullopt; }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    fastgltf::Parser gltfparser;
    const auto gltfasset = [&file_path, &gltfparser, &fastdatabuf] {
        ScopedTimer timer{ ENG_FMT("GLTF file parsing {}", file_path.string()) };
        if(file_path.extension() == ".glb")
        {
            return gltfparser.loadGltfBinary(fastdatabuf.get(), file_path.parent_path(), gltfOptions);
        }
        else if(file_path.extension() == ".gltf")
        {
            return gltfparser.loadGltfJson(fastdatabuf.get(), file_path.parent_path(), gltfOptions);
        }
    }();

    if(!gltfasset) { return std::nullopt; }
    if(gltfasset->scenes.empty()) { return std::nullopt; }

    auto& gltfscene = gltfasset->scenes.at(0);

    ScopedTimer timer{ ENG_FMT("GLTF loading asset {}", file_path.string()) };
    Asset asset{};
    gltf::Context context{};
    context.import_settings = import_settings;
    for(auto i = 0u; i < gltfscene.nodeIndices.size(); ++i)
    {
        uint32_t out_index;
        gltf::load_node(asset, gltfasset.get<1>(), gltfasset->nodes[gltfscene.nodeIndices[i]], nullptr, context, &out_index);
        asset.root_nodes.push_back(out_index);
    }

    return asset;
}

} // namespace assets
} // namespace eng