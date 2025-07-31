#include <stack>
#include <ranges>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb/stb_image.h>
#include <eng/scene.hpp>
#include <eng/engine.hpp>
#include <eng/common/components.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/paths.hpp>

static Handle<gfx::Geometry> load_geometry(const fastgltf::Asset& asset, const fastgltf::Mesh& mesh,
                                           uint32_t primitive_index, scene::LoadedNode& ctx)
{
    const auto& primitive = mesh.primitives.at(primitive_index);
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    if(auto it = primitive.findAttribute("POSITION"); it != primitive.attributes.end())
    {
        auto& acc = asset.accessors.at(it->accessorIndex);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return {};
        }
        vertices.resize(acc.count);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, acc, [&vertices](const auto& vec, auto idx) {
            vertices.at(idx).position = { vec.x(), vec.y(), vec.z() };
        });
    }
    else
    {
        ENG_WARN("Mesh primitive does not contain position. Skipping...");
        return {};
    }
    if(auto it = primitive.findAttribute("NORMAL"); it != primitive.attributes.end())
    {
        auto& acc = asset.accessors.at(it->accessorIndex);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return {};
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, acc, [&vertices](const auto& vec, auto idx) {
            vertices.at(idx).normal = { vec.x(), vec.y(), vec.z() };
        });
    }
    if(auto it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end())
    {
        auto& acc = asset.accessors.at(it->accessorIndex);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return {};
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, acc, [&vertices](const auto& vec, auto idx) {
            vertices.at(idx).uv = { vec.x(), vec.y() };
        });
    }
    if(auto it = primitive.findAttribute("TANGENT"); it != primitive.attributes.end())
    {
        auto& acc = asset.accessors.at(it->accessorIndex);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return {};
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, acc, [&vertices](const auto& vec, auto idx) {
            vertices.at(idx).tangent = { vec.x(), vec.y(), vec.z(), vec.w() };
        });
    }
    if(!primitive.indicesAccessor)
    {
        ENG_WARN("Mesh primitive {}:{} does not have mandatory vertex indices. Skipping...", mesh.name.c_str(), primitive_index);
        return {};
    }
    else
    {
        auto& acc = asset.accessors.at(*primitive.indicesAccessor);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return {};
        }
        indices.resize(acc.count);
        fastgltf::copyFromAccessor<uint32_t>(asset, acc, indices.data());
    }

    const auto geom = Engine::get().renderer->batch_geometry(gfx::GeometryDescriptor{ .vertices = vertices, .indices = indices });
    ctx.geometries.push_back(geom);
    return geom;
}

static Handle<gfx::Image> load_image(const fastgltf::Asset& asset, gfx::ImageFormat format, size_t index, scene::LoadedNode& ctx)
{
    if(index == ~0ull) { return {}; }
    if(ctx.images.size() <= index) { ctx.images.resize(asset.images.size()); }
    // todo: check if image format matches with the currently requested.
    if(ctx.images.at(index)) { return ctx.images.at(index); }

    const auto& fimg = asset.images.at(index);
    std::span<const std::byte> data;
    if(auto fdatasrcview = std::get_if<fastgltf::sources::BufferView>(&fimg.data))
    {
        auto& fdataview = asset.bufferViews.at(fdatasrcview->bufferViewIndex);
        auto& fdataviewbuf = asset.buffers.at(fdataview.bufferIndex);
        if(auto fdataviewbufsrc = std::get_if<fastgltf::sources::Array>(&fdataviewbuf.data))
        {
            data = { fdataviewbufsrc->bytes.data() + fdataview.byteOffset, fdataview.byteLength };
        }
    }
    if(data.empty())
    {
        ENG_WARN("Could not load image {}", fimg.name.c_str());
        return {};
    }

    auto imgd = gfx::ImageDescriptor{};
    imgd.name = fimg.name.c_str();
    int x, y, ch;
    std::byte* imgdata = reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                                            data.size(), &x, &y, &ch, 4));
    if(!imgdata)
    {
        ENG_ERROR("Stbi failed for image {}: {}", fimg.name.c_str(), stbi_failure_reason());
        return {};
    }

    imgd.data = { imgdata, imgdata + x * y * ch };
    imgd.width = (uint32_t)x;
    imgd.height = (uint32_t)y;
    const auto img = Engine::get().renderer->batch_image(imgd);
    ctx.images.at(index) = img;
    return img;
}

static Handle<gfx::Sampler> load_sampler(const fastgltf::Asset& asset, size_t index, scene::LoadedNode& ctx)
{
    if(index == ~0ull) { return Engine::get().renderer->batch_sampler(gfx::SamplerDescriptor{}); }
    if(ctx.samplers.size() <= index) { ctx.samplers.resize(asset.samplers.size()); }
    if(ctx.samplers.at(index)) { return ctx.samplers.at(index); }

    const auto& fsamp = asset.samplers.at(index);
    auto sampd = gfx::SamplerDescriptor{};
    if(fsamp.minFilter)
    {
        if(*fsamp.minFilter == fastgltf::Filter::Nearest) { sampd.filtering[0] = gfx::ImageFilter::NEAREST; }
        else if(*fsamp.minFilter == fastgltf::Filter::Linear) { sampd.filtering[0] = gfx::ImageFilter::LINEAR; }
        else if(*fsamp.minFilter == fastgltf::Filter::LinearMipMapNearest)
        {
            sampd.filtering[0] = gfx::ImageFilter::NEAREST;
            sampd.mipmap_mode = gfx::SamplerDescriptor::MipMapMode::NEAREST;
        }
        else if(*fsamp.minFilter == fastgltf::Filter::LinearMipMapLinear)
        {
            sampd.filtering[0] = gfx::ImageFilter::LINEAR;
            sampd.mipmap_mode = gfx::SamplerDescriptor::MipMapMode::LINEAR;
        }
    }
    if(fsamp.magFilter)
    {
        if(*fsamp.magFilter == fastgltf::Filter::Nearest) { sampd.filtering[1] = gfx::ImageFilter::NEAREST; }
        else if(*fsamp.magFilter == fastgltf::Filter::Linear) { sampd.filtering[1] = gfx::ImageFilter::LINEAR; }
    }
    const auto sampler = Engine::get().renderer->batch_sampler(sampd);
    ctx.samplers.at(index) = sampler;
    return sampler;
}

static Handle<gfx::Texture> load_texture(const fastgltf::Asset& asset, gfx::ImageFormat format, size_t index, scene::LoadedNode& ctx)
{
    if(index == ~0ull) { return {}; }
    if(ctx.textures.size() <= index) { ctx.textures.resize(asset.textures.size()); }
    if(ctx.textures.at(index)) { return ctx.textures.at(index); }

    const auto& ftex = asset.textures.at(index);
    const auto tex = Engine::get().renderer->batch_texture(gfx::TextureDescriptor{
        .image = load_image(asset, format, ftex.imageIndex ? *ftex.imageIndex : ~0ull, ctx),
        .sampler = load_sampler(asset, ftex.samplerIndex ? *ftex.samplerIndex : ~0ull, ctx) });
    ctx.textures.at(index) = tex;
    return tex;
}

static Handle<gfx::Material> load_material(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, scene::LoadedNode& ctx)
{
    if(!primitive.materialIndex) { return {}; }
    if(ctx.materials.size() <= *primitive.materialIndex) { ctx.materials.resize(asset.materials.size()); }
    if(ctx.materials.at(*primitive.materialIndex)) { return ctx.materials.at(*primitive.materialIndex); }

    const auto& fmat = asset.materials.at(*primitive.materialIndex);
    const auto mat = Engine::get().renderer->batch_material(gfx::MaterialDescriptor{
        .base_color_texture = load_texture(asset, gfx::ImageFormat::R8G8B8A8_SRGB,
                                           fmat.pbrData.baseColorTexture ? fmat.pbrData.baseColorTexture->textureIndex : ~0ull, ctx),
        .normal_texture = load_texture(asset, gfx::ImageFormat::R8G8B8A8_UNORM,
                                       fmat.normalTexture ? fmat.normalTexture->textureIndex : ~0ull, ctx),
        .metallic_roughness_texture =
            load_texture(asset, gfx::ImageFormat::R8G8B8A8_UNORM,
                         fmat.pbrData.metallicRoughnessTexture ? fmat.pbrData.metallicRoughnessTexture->textureIndex : ~0ull, ctx),
    });
    ctx.materials.at(*primitive.materialIndex) = mat;
    return mat;
}

static void load_mesh(ecs::Entity e, const fastgltf::Asset& asset, const fastgltf::Node& node, scene::LoadedNode& ctx)
{
    auto* ecsr = Engine::get().ecs;
    if(!node.meshIndex) { return; }

    const auto& fm = asset.meshes.at(*node.meshIndex);
    if(ctx.meshes.size() <= *node.meshIndex) { ctx.meshes.resize(asset.meshes.size()); }
    if(ctx.meshes.at(*node.meshIndex).meshes.size())
    {
        ecsr->emplace<ecs::comp::MeshRenderer>(e, ctx.meshes.at(*node.meshIndex));
        return;
    }

    auto& m = ctx.meshes.at(*node.meshIndex);
    m.name = fm.name.c_str();
    m.meshes.resize(fm.primitives.size());
    for(auto i = 0u; i < fm.primitives.size(); ++i)
    {
        const auto geom = load_geometry(asset, fm, i, ctx);
        const auto mat = load_material(asset, fm.primitives.at(i), ctx);
        m.meshes.at(i) = Engine::get().renderer->batch_mesh(gfx::MeshDescriptor{ .geometry = geom, .material = mat });
    }
    ecsr->emplace<ecs::comp::MeshRenderer>(e, m);
}

static ecs::Entity load_node(const fastgltf::Scene& scene, const fastgltf::Asset& asset, const fastgltf::Node& node,
                             scene::LoadedNode& ctx, glm::mat4 transform = { 1.0f })
{
    auto* ecsr = Engine::get().ecs;
    auto entity = ecsr->create();

    ecsr->emplace<ecs::comp::Node>(entity, ecs::comp::Node{ node.name.c_str() });

    const auto& trs = std::get<fastgltf::TRS>(node.transform);
    const auto glm_local =
        glm::translate(glm::mat4{ 1.0f }, glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() }) *
        glm::mat4_cast(glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() }) *
        glm::scale(glm::mat4{ 1.0f }, glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
    const auto glm_global = glm_local * transform;
    ecsr->emplace<ecs::comp::Transform>(entity, ecs::comp::Transform{ .local = glm_local, .global = glm_global });

    load_mesh(entity, asset, node, ctx);

    for(auto e : node.children)
    {
        ecsr->make_child(entity, load_node(scene, asset, asset.nodes.at(e), ctx, glm_global));
    }

    return entity;
}

namespace scene
{

ecs::Entity Scene::load_from_file(const std::filesystem::path& _path)
{
    const auto filepath = paths::canonize_path(_path, "models");

    if(const auto it = nodes.find(filepath); it != nodes.end()) { return it->second.root; }

    if(filepath.extension() != ".glb")
    {
        ENG_WARN("Only glb files are supported.");
        return {};
    }

    if(!std::filesystem::exists(filepath))
    {
        ENG_WARN("Path {} does not point to any file.", filepath.string());
        return {};
    }

    fastgltf::Parser fastparser;
    auto fastglbbuf = fastgltf::GltfDataBuffer::FromPath(filepath);
    if(!fastglbbuf)
    {
        ENG_WARN("Error during fastgltf::GltfDataBuffer import: {}", fastgltf::getErrorName(fastglbbuf.error()));
        return {};
    }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    auto fassetexp = fastparser.loadGltfBinary(fastglbbuf.get(), filepath.parent_path(), gltfOptions);
    if(!fassetexp)
    {
        ENG_WARN("Error during loading fastgltf::Parser::loadGltfBinary: {}", fastgltf::getErrorName(fassetexp.error()));
        return {};
    }

    auto& fasset = fassetexp.get();
    if(fasset.scenes.empty())
    {
        ENG_WARN("Error during loading. Fastgltf asset does not have any scenes defined.");
        return {};
    }

    auto& fscene = fasset.scenes.at(0);
    auto* r = Engine::get().renderer;
    auto* ecsr = Engine::get().ecs;
    scene::LoadedNode ctx;

    auto root = ecsr->create();
    for(const auto fsni : fscene.nodeIndices)
    {
        ecsr->make_child(root, load_node(fscene, fasset, fasset.nodes.at(fsni), ctx));
    }
    ctx.root = root;
    nodes[filepath] = std::move(ctx);
    return root;
}

ecs::Entity Scene::instance_entity(ecs::Entity node)
{
    auto* ecs = Engine::get().ecs;
    auto* r = Engine::get().renderer;
    auto root = ecs->clone(node);
    ecs->traverse_hierarchy(root, [ecs, r](auto p, auto e) {
        auto* mr = ecs->get<ecs::comp::MeshRenderer>(e);
        if(mr) { r->instance_mesh(gfx::InstanceSettings{ .entity = e }); }
    });
    scene.push_back(root);
    return root;
}

} // namespace scene
