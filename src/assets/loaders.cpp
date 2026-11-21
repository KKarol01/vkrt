#include <eng/assets/loaders.hpp>

#include <variant>
#include <vector>
#include <cstddef>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <meshoptimizer/src/meshoptimizer.h>

#include <eng/engine.hpp>
#include <eng/ecs/components.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <assets/shaders/common.hlsli>

namespace eng
{
namespace assets
{

namespace gltf
{

struct Context
{
    Flags<ImportSettings> import_settings;
    std::vector<u32> images;
    std::vector<u32> textures;
    std::vector<Range32u> materials;
    std::vector<Range32u> geometries;
    std::vector<Range32u> meshes;
    gfx::ICommandBuffer* cmd;
};

static constexpr size_t ATTRIBUTE_FLOATS =
    (gfx::get_vertex_layout_size(gfx::VertexComponent::ALL) - gfx::get_vertex_layout_size(gfx::VertexComponent::POSITION_BIT)) /
    sizeof(float);

gfx::ParsedGeometryData meshletize(const std::vector<float>& positions, const std::vector<float>& attributes,
                                   const std::vector<u32>& indices)
{
    static constexpr auto max_verts = 64u;
    static constexpr auto max_tris = 124u;
    static constexpr auto cone_weight = 0.0f;

    const auto max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_verts, max_tris);
    std::vector<meshopt_Meshlet> mlts(max_meshlets);
    std::vector<u32> mlt_vtxs(max_meshlets * max_verts);
    std::vector<u8> mlt_tris(max_meshlets * max_tris * 3);

    const auto vtx_count = positions.size() / 3;
    const auto pos_stride = 3 * sizeof(float);
    const auto mltcnt = meshopt_buildMeshlets(mlts.data(), mlt_vtxs.data(), mlt_tris.data(), indices.data(), indices.size(),
                                              positions.data(), vtx_count, pos_stride, max_verts, max_tris, cone_weight);

    mlts.resize(mltcnt);
    const auto& last = mlts.back();
    mlt_vtxs.resize(last.vertex_offset + last.vertex_count);
    mlt_tris.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

    gfx::ParsedGeometryData geom{ .vertex_layout = gfx::VertexComponent::ALL };
    geom.meshlets.resize(mltcnt);
    for(size_t i = 0; i < mltcnt; ++i)
    {
        auto& m = mlts[i];
        meshopt_optimizeMeshlet(&mlt_vtxs[m.vertex_offset], &mlt_tris[m.triangle_offset], m.triangle_count, m.vertex_count);
        const auto b = meshopt_computeMeshletBounds(&mlt_vtxs[m.vertex_offset], &mlt_tris[m.triangle_offset],
                                                    m.triangle_count, positions.data(), vtx_count, pos_stride);
        geom.meshlets[i] = gfx::Meshlet{ .vertex_offset = (i32)m.vertex_offset,
                                         .vertex_count = m.vertex_count,
                                         .index_offset = m.triangle_offset,
                                         .index_count = m.triangle_count * 3,
                                         .bounding_sphere = { b.center[0], b.center[1], b.center[2], b.radius } };
    }

    geom.positions.resize(mlt_vtxs.size() * 3);
    geom.attributes.resize(mlt_vtxs.size() * ATTRIBUTE_FLOATS);
    for(size_t i = 0; i < mlt_vtxs.size(); ++i)
    {
        memcpy(&geom.positions[i * 3], &positions[mlt_vtxs[i] * 3], 3 * sizeof(float));
        memcpy(&geom.attributes[i * ATTRIBUTE_FLOATS], &attributes[mlt_vtxs[i] * ATTRIBUTE_FLOATS],
               ATTRIBUTE_FLOATS * sizeof(float));
    }

    geom.indices.resize(mlt_tris.size());
    gfx::copy_indices(std::as_writable_bytes(std::span{ geom.indices }), std::as_bytes(std::span{ mlt_tris }),
                      gfx::IndexFormat::U16, gfx::IndexFormat::U8);
    return geom;
}

Range32u load_geometry(Asset& asset, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Context& ctx)
{
    if(ctx.geometries.empty()) { ctx.geometries.insert(ctx.geometries.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.geometries[gltfmeshidx].size != 0u) { return ctx.geometries[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];

    Range32u geoms{ (u32)asset.geometries.size(), 0u };
    for(auto i = 0ull; i < gltfmesh.primitives.size(); ++i)
    {
        const fastgltf::Primitive& gltfprim = gltfmesh.primitives[i];

        const auto posit = gltfprim.findAttribute("POSITION");
        if(posit == gltfprim.attributes.end())
        {
            ENG_ERROR("Mesh {} primitive {} does not have positions. Skipping...", gltfmesh.name.c_str(), i);
            continue;
        }
        if(!gltfprim.indicesAccessor)
        {
            ENG_WARN("Mesh {} primitive {} does not have mandatory vertex indices. Skipping...", gltfmesh.name.c_str(), i);
            continue;
        }
        const auto& idxacc = gltfasset.accessors.at(*gltfprim.indicesAccessor);
        if(!idxacc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            continue;
        }

        const auto& posacc = gltfasset.accessors.at(posit->accessorIndex);
        std::vector<float> positions(posacc.count * 3);
        std::vector<float> attributes(posacc.count * ATTRIBUTE_FLOATS);
        std::vector<u32> indices(idxacc.count);
        fastgltf::copyFromAccessor<fastgltf::math::fvec3>(gltfasset, posacc, positions.data());
        fastgltf::copyFromAccessor<u32>(gltfasset, idxacc, indices.data());

        const auto copy_attribute = [&]<size_t N>(const char* name, gfx::VertexComponent comp) {
            const auto it = gltfprim.findAttribute(name);
            if(it == gltfprim.attributes.end()) { return; }
            const auto offset = gfx::get_vertex_component_offset(comp) / sizeof(float) - 3;
            fastgltf::iterateAccessorWithIndex<fastgltf::math::vec<float, N>>(
                gltfasset, gltfasset.accessors.at(it->accessorIndex), [&](const auto& v, size_t vidx) {
                    for(size_t k = 0; k < N; ++k)
                    {
                        attributes[vidx * ATTRIBUTE_FLOATS + offset + k] = v[k];
                    }
                });
        };
        copy_attribute.template operator()<3>("NORMAL", gfx::VertexComponent::NORMAL_BIT);
        copy_attribute.template operator()<4>("TANGENT", gfx::VertexComponent::TANGENT_BIT);
        copy_attribute.template operator()<2>("TEXCOORD_0", gfx::VertexComponent::UV0_BIT);

        auto geom = meshletize(positions, attributes, indices);
        asset.geometries.push_back(gfx::get_renderer().make_geometry(gfx::GeometryDescriptor{
            .vertex_layout = geom.vertex_layout,
            .index_format = gfx::IndexFormat::U16,
            .vertices = geom.positions,
            .attributes = geom.attributes,
            .indices = std::as_bytes(std::span{ geom.indices }),
            .meshlets = geom.meshlets,
        }));
        if(ctx.import_settings.test(ImportSettings::KEEP_DATA_BIT)) { asset.geometry_data.push_back(std::move(geom)); }
        ++geoms.size;
    }

    ctx.geometries[gltfmeshidx] = geoms;
    return geoms;
}

u32 load_image(Asset& asset, const fastgltf::Asset& gltfasset, gfx::ImageFormat format, size_t gltfimgidx, Context& ctx)
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

    const auto img = gfx::get_renderer().make_image(gltfimg.name.c_str(),
                                                    gfx::Image::init((u32)x, (u32)y, 0, format,
                                                                     gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                         gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                     0, 1, gfx::ImageLayout::READ_ONLY));
    if(!img) { ENG_ERROR("Failed to create image{}", gltfimg.name.c_str()); }
    else
    {
        ENG_ASSERT(ctx.cmd);
        gfx::get_renderer().staging->copy(img.get(), imgdata, 0, 0, false, gfx::DiscardContents::YES);
        ctx.cmd->generate_mips(img.get());
    }

    if(!img) { return ~0u; }

    const u32 imgidx = asset.images.size();
    asset.images.push_back(img);
    ctx.images[gltfimgidx] = imgidx;

    if(ctx.import_settings & ImportSettings::KEEP_DATA_BIT)
    {
        asset.image_data.emplace_back(gltfimg.name.c_str(), (u32)x, (u32)y, format,
                                      std::vector<std::byte>(imgdata, imgdata + x * y * 4));
    }
    stbi_image_free(imgdata);

    return imgidx;
}

u32 load_texture(Asset& asset, const fastgltf::Asset& gltfasset, gfx::ImageFormat format, size_t gltftexidx, Context& ctx)
{
    if(ctx.textures.empty()) { ctx.textures.insert(ctx.textures.begin(), gltfasset.textures.size(), ~0u); }
    if(ctx.textures[gltftexidx] != ~0u) { return ctx.textures[gltftexidx]; }
    const auto& gltftex = gltfasset.textures[gltftexidx];
    if(!gltftex.imageIndex)
    {
        ENG_ERROR("Texture {} does not have associated image", gltftex.name.c_str());
        return ~0u;
    }
    u32 image = load_image(asset, gltfasset, format, *gltftex.imageIndex, ctx);
    if(image == ~0u) { return ~0u; }

    u32 texidx = asset.textures.size();
    asset.textures.push_back(gfx::ImageView::init(asset.images[image], format));
    ctx.textures[gltftexidx] = texidx;
    return texidx;
}

Range32u load_material(Asset& asset, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Context& ctx)
{
    if(ctx.materials.empty()) { ctx.materials.insert(ctx.materials.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.materials[gltfmeshidx].size != 0u) { return ctx.materials[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];
    Range32u mats{ (u32)asset.materials.size(), 0u };
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

        switch(gltfmat.alphaMode)
        {
        case fastgltf::AlphaMode::Opaque:
        {
            mat.mode = gfx::Material::Mode::OPAQUE;
            break;
        }
        case fastgltf::AlphaMode::Mask:
        {
            mat.mode = gfx::Material::Mode::ALPHA;
            mat.alpha_cutoff = gltfmat.alphaCutoff;
            break;
        }
        case fastgltf::AlphaMode::Blend:
        {
            mat.mode = gfx::Material::Mode::TRANSPARENT;
            mat.base_color_factor = pack_unorm4x8(gltfmat.pbrData.baseColorFactor.x(), gltfmat.pbrData.baseColorFactor.y(),
                                                  gltfmat.pbrData.baseColorFactor.z(), gltfmat.pbrData.baseColorFactor.w());
            break;
        }
        default:
        {
            ENG_WARN("Unandled alpha mode");
        }
        }

        if(gltfmat.pbrData.baseColorTexture)
        {
            u32 texidx = load_texture(asset, gltfasset, gfx::ImageFormat::R8G8B8A8_SRGB,
                                      gltfmat.pbrData.baseColorTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                mat.base_color_texture = asset.textures[ctx.textures[gltfmat.pbrData.baseColorTexture->textureIndex]];
            }
        }
        if(gltfmat.normalTexture)
        {
            u32 texidx =
                load_texture(asset, gltfasset, gfx::ImageFormat::R8G8B8A8_UNORM, gltfmat.normalTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                mat.normal_texture = asset.textures[ctx.textures[gltfmat.normalTexture->textureIndex]];
            }
        }
        if(gltfmat.pbrData.metallicRoughnessTexture)
        {
            u32 texidx = load_texture(asset, gltfasset, gfx::ImageFormat::R8G8B8A8_UNORM,
                                      gltfmat.pbrData.metallicRoughnessTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                mat.metallic_roughness_texture =
                    asset.textures[ctx.textures[gltfmat.pbrData.metallicRoughnessTexture->textureIndex]];
            }
        }

        asset.materials.push_back(gfx::get_renderer().make_material(mat));
        ++mats.size;
    }

    ctx.materials[gltfmeshidx] = mats;
    return mats;
}

Range32u load_mesh(Asset& asset, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Node& node, Context& ctx)
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
    ENG_ASSERT(geoms == mats && geoms.offset == (u32)asset.meshes.size());
    for(auto i = 0u; i < geoms.size; ++i)
    {
        asset.meshes.push_back(gfx::get_renderer().make_mesh(gfx::MeshDescriptor{
            .geometry = asset.geometries[geoms.offset + i], .material = asset.materials[mats.offset + i] }));
    }
    ctx.meshes[gltfmeshidx] = geoms;
    return geoms;
}

Node load_node(Asset& asset, const fastgltf::Asset& gltfasset, const fastgltf::Node& gltfnode, Node* parent_node,
               Context& context, u32* out_node_index)
{
    Node node{};
    node.name = gltfnode.name.c_str();
    node.transform = [&] {
        auto& transform = asset.transforms.emplace_back();
        const auto parent_transform =
            parent_node ? asset.transforms[parent_node->transform].to_mat4() : glm::identity<glm::mat4>();
        if(gltfnode.transform.index() == 0)
        {
            const auto& gltftrs = std::get<fastgltf::TRS>(gltfnode.transform);
            ecsc::Transform trs{};
            trs.position = glm::vec3{ gltftrs.translation.x(), gltftrs.translation.y(), gltftrs.translation.z() };
            trs.rotation = glm::quat{ gltftrs.rotation.w(), gltftrs.rotation.x(), gltftrs.rotation.y(), gltftrs.rotation.z() };
            trs.scale = glm::vec3{ gltftrs.scale.x(), gltftrs.scale.y(), gltftrs.scale.z() };
            transform = ecsc::Transform::init(parent_transform * trs.to_mat4());
        }
        else
        {
            const auto& fasttrs = std::get<fastgltf::math::fmat4x4>(gltfnode.transform);
            glm::mat4 trs;
            memcpy(&trs, &fasttrs, sizeof(fasttrs));
            transform = ecsc::Transform::init(parent_transform * trs);
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

    std::vector<Node> children;
    children.reserve(gltfnode.children.size());
    for(const auto& e : gltfnode.children)
    {
        u32 out_idx;
        children.push_back(load_node(asset, gltfasset, gltfasset.nodes[e], &node, context, &out_idx));
    }
    const auto nodeidx = (u32)asset.nodes.size();
    node.children = { (u32)asset.nodes.size() + 1, (u32)children.size() };
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

std::optional<Asset> try_load_gltf_from_virtual_path(const fs::Path& file_path, Flags<ImportSettings> import_settings)
{
    const auto ext = file_path.extension();
    if(ext != ".glb" && ext != ".gltf") { return std::nullopt; }

    auto file = Engine::get().fs->open_file(file_path, fs::OpenMode::READ_BYTES);
    if(!file) { return std::nullopt; }
    const auto file_bytes = file.read();
    auto fastdatabuf = fastgltf::GltfDataBuffer::FromBytes((const std::byte*)file_bytes.data(), file_bytes.size());
    if(!fastdatabuf) { return std::nullopt; }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    fastgltf::Parser gltfparser;
    // external buffers/images are resolved by fastgltf directly, so it needs the physical directory (relative to root)
    const auto gltfasset = [&] {
        ENG_TIMER_SCOPED("GLTF file parsing {}", file_path.string());
        return gltfparser.loadGltf(fastdatabuf.get(), file_path.parent_path().relative_path(), gltfOptions);
    }();

    if(!gltfasset) { return std::nullopt; }
    if(gltfasset->scenes.empty()) { return std::nullopt; }

    auto& gltfscene = gltfasset->scenes.at(0);

    ENG_TIMER_SCOPED("GLTF loading asset {}", file_path.string());
    Asset asset{};
    gltf::Context ctx{};
    ctx.import_settings = import_settings;
    ctx.cmd = gfx::get_renderer().current_data->cmdpool->begin();
    for(auto i = 0u; i < gltfscene.nodeIndices.size(); ++i)
    {
        u32 out_index;
        gltf::load_node(asset, gltfasset.get<1>(), gltfasset->nodes[gltfscene.nodeIndices[i]], nullptr, ctx, &out_index);
        asset.root_nodes.push_back(out_index);
    }
    auto* staging_sync = gfx::get_renderer().staging->flush(true);
    auto* mip_sync = gfx::get_renderer().current_data->get_sync();
    ctx.cmd->wait_sync(staging_sync, gfx::PipelineStage::TRANSFER_BIT);
    ctx.cmd->signal_sync(mip_sync, gfx::PipelineStage::FRAGMENT_BIT);
    gfx::get_renderer().current_data->cmdpool->end(ctx.cmd);
    gfx::get_renderer().gq->with_cmd_buf(ctx.cmd).submit();
    gfx::get_renderer().current_data->wait_syncs.push_back(mip_sync);

    asset.path = file_path;
    return asset;
}

} // namespace assets
} // namespace eng