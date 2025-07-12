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

namespace scene
{

ecs::Entity Scene::load_from_file(const std::filesystem::path& _path)
{
    const auto filepath = paths::canonize_path(_path, "models");

    if(const auto it = nodes.find(filepath); it != nodes.end()) { return it->second; }

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
    auto fgltfe = fastparser.loadGltfBinary(fastglbbuf.get(), filepath.parent_path(), gltfOptions);
    if(!fgltfe)
    {
        ENG_WARN("Error during loading fastgltf::Parser::loadGltfBinary: {}", fastgltf::getErrorName(fgltfe.error()));
        return {};
    }

    auto& fgltfa = fgltfe.get();
    if(fgltfa.scenes.empty())
    {
        ENG_WARN("Error during loading. Fastgltf asset does not have any scenes defined.");
        return {};
    }

    auto& fgltfs = fgltfa.scenes.at(0);
    auto* r = Engine::get().renderer;
    auto* ecsr = Engine::get().ecs;

    std::vector<gfx::ImageDescriptor> bimgs;
    std::vector<void*> bimgs_to_delete;
    std::vector<gfx::TextureDescriptor> btxts;
    std::vector<gfx::MaterialDescriptor> bmats;
    std::vector<Handle<gfx::Geometry>> hgeoms;
    std::vector<std::vector<gfx::MeshDescriptor>> bmeshes;
    bimgs.reserve(fgltfa.images.size());
    bimgs_to_delete.reserve(fgltfa.images.size());
    btxts.reserve(fgltfa.textures.size());
    bmats.reserve(fgltfa.materials.size());
    bmats.reserve(fgltfa.meshes.size());
    bmeshes.reserve(fgltfa.meshes.size());

    for(auto i = 0u; i < fgltfa.images.size(); ++i)
    {
        using namespace fastgltf;
        auto& image = bimgs.emplace_back();
        auto& fgltfimg = fgltfa.images.at(i);
        std::span<const std::byte> data;
        if(auto fastsrcbview = std::get_if<sources::BufferView>(&fgltfimg.data))
        {
            auto& fastbview = fgltfa.bufferViews.at(fastsrcbview->bufferViewIndex);
            auto& fastbuf = fgltfa.buffers.at(fastbview.bufferIndex);
            if(auto fastsrcarr = std::get_if<sources::Array>(&fastbuf.data))
            {
                data = { fastsrcarr->bytes.data() + fastbview.byteOffset, fastbview.byteLength };
            }
        }
        if(!data.empty())
        {
            image.name = fgltfimg.name.c_str();
            int x, y, ch;
            std::byte* imgdata =
                reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                                   data.size(), &x, &y, &ch, 4));
            if(!imgdata) { ENG_ERROR("Stbi failed: {}", stbi_failure_reason()); }
            else
            {
                image.data = { imgdata, imgdata + x * y * ch };
                image.width = (uint32_t)x;
                image.height = (uint32_t)y;
                bimgs_to_delete.push_back(imgdata);
            }
        }
        else { ENG_WARN("Could not load image {}", fgltfimg.name.c_str()); }
    }

    for(auto i = 0u; i < fgltfa.textures.size(); ++i)
    {
        auto& fasttxt = fgltfa.textures.at(i);
        auto& txt = btxts.emplace_back();
        if(!fasttxt.imageIndex) { ENG_WARN("Unsupported texture {} type.", fasttxt.name.c_str()); }
        else
        {
            if(fasttxt.samplerIndex)
            {
                auto& fsamp = fgltfa.samplers.at(*fasttxt.samplerIndex);
                ENG_TODO("Implement sampler settings import from fastgltf");
            }
            txt.image = Handle<gfx::Image>{ *fasttxt.imageIndex };
        }
    }

    for(auto i = 0u; i < fgltfa.materials.size(); ++i)
    {
        auto& fastmat = fgltfa.materials.at(i);
        auto& mat = bmats.emplace_back();
        // mat.name = fastmat.name.c_str();
        if(fastmat.pbrData.baseColorTexture)
        {
            assert(fastmat.pbrData.baseColorTexture->texCoordIndex == 0);
            auto& txt = btxts.at(fastmat.pbrData.baseColorTexture->textureIndex);
            auto& img = bimgs.at(*txt.image);
            img.format = gfx::ImageFormat::R8G8B8A8_SRGB;
            mat.base_color_texture = Handle<gfx::Texture>{ fastmat.pbrData.baseColorTexture->textureIndex };
        }
    }

    for(auto i = 0u; i < fgltfa.meshes.size(); ++i)
    {
        auto& fgltfmesh = fgltfa.meshes.at(i);
        std::vector<gfx::MeshDescriptor> submeshes;
        for(auto j = 0u; j < fgltfmesh.primitives.size(); ++j)
        {
            const auto load_primitive = [&]() -> gfx::MeshDescriptor {
                gfx::MeshDescriptor mesh{};
                auto& fgltfprim = fgltfmesh.primitives.at(j);
                std::vector<gfx::Vertex> vertices;
                std::vector<uint32_t> indices;
                if(auto it = fgltfprim.findAttribute("POSITION"); it != fgltfprim.attributes.end())
                {
                    auto& acc = fgltfa.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return mesh;
                    }
                    vertices.resize(acc.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fgltfa, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).position = { vec.x(), vec.y(), vec.z() };
                    });
                }
                else
                {
                    ENG_WARN("Mesh primitive does not contain position. Skipping...");
                    return mesh;
                }
                if(auto it = fgltfprim.findAttribute("NORMAL"); it != fgltfprim.attributes.end())
                {
                    auto& acc = fgltfa.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return mesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fgltfa, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).normal = { vec.x(), vec.y(), vec.z() };
                    });
                }
                if(auto it = fgltfprim.findAttribute("TEXCOORD_0"); it != fgltfprim.attributes.end())
                {
                    auto& acc = fgltfa.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return mesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(fgltfa, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).uv = { vec.x(), vec.y() };
                    });
                }
                if(auto it = fgltfprim.findAttribute("TANGENT"); it != fgltfprim.attributes.end())
                {
                    auto& acc = fgltfa.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return mesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(fgltfa, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).tangent = { vec.x(), vec.y(), vec.z(), vec.w() };
                    });
                }
                if(!fgltfprim.indicesAccessor)
                {
                    ENG_WARN("Mesh primitive {}:{} does not have mandatory vertex indices. Skipping...",
                             fgltfmesh.name.c_str(), j);
                    return mesh;
                }
                else
                {
                    auto& acc = fgltfa.accessors.at(*fgltfprim.indicesAccessor);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return mesh;
                    }
                    indices.resize(acc.count);
                    fastgltf::copyFromAccessor<uint32_t>(fgltfa, acc, indices.data());
                }
                if(fgltfprim.materialIndex) { mesh.material = Handle<gfx::Material>{ *fgltfprim.materialIndex }; }
                else { ENG_WARN("Submesh #{} in mesh {} does not have a material", fgltfmesh.name.c_str(), j); }

                mesh.geometry = r->batch_geometry(gfx::GeometryDescriptor{
                    .vertices = vertices,
                    .indices = indices,
                });
                return mesh;
            };
            submeshes.push_back(load_primitive());
        }
        bmeshes.push_back(submeshes);
    }

    std::vector<Handle<gfx::Image>> himgs;
    std::vector<Handle<gfx::Texture>> htxts;
    std::vector<Handle<gfx::Material>> hmats;
    std::vector<std::vector<Handle<gfx::Mesh>>> hmeshes;
    himgs.reserve(bimgs.size());
    htxts.reserve(btxts.size());
    hmats.reserve(bmats.size());
    hmeshes.reserve(bmeshes.size());
    for(auto& e : bimgs)
    {
        himgs.push_back(r->batch_image(e));
    }
    for(auto& e : bimgs_to_delete)
    {
        stbi_image_free(e);
    }
    for(auto& e : btxts)
    {
        e.image = himgs.at(*e.image);
        htxts.push_back(r->batch_texture(e));
    }
    for(auto& e : bmats)
    {
        e.base_color_texture = htxts.at(*e.base_color_texture); // todo: fix the rest of the textures;
        hmats.push_back(r->batch_material(e));
    }
    for(auto& b : bmeshes)
    {
        auto& hms = hmeshes.emplace_back();
        hms.reserve(b.size());
        for(auto& bm : b)
        {
            if(bm.material) { bm.material = hmats.at(*bm.material); }
            hms.push_back(r->batch_mesh(bm));
        }
    }
    geometries.insert(geometries.end(), hgeoms.begin(), hgeoms.end());
    images.insert(images.end(), himgs.begin(), himgs.end());
    textures.insert(textures.end(), htxts.begin(), htxts.end());
    materials.insert(materials.end(), hmats.begin(), hmats.end());
    meshes.insert(meshes.end(), hmeshes.begin(), hmeshes.end());

    ecs::Entity ecsroot = ecsr->create();
    ecsr->emplace<ecs::comp::Transform>(ecsroot);
    ecsr->emplace<ecs::comp::Node>(ecsroot, ecs::comp::Node{ .name = filepath.filename().string() });

    std::vector<ecs::Entity> bnodes;
    bnodes.reserve(fgltfa.nodes.size());
    for(auto i = 0u; i < fgltfa.nodes.size(); ++i)
    {
        const auto& fgltfnode = fgltfa.nodes.at(i);
        ecs::Entity node = ecsr->create();
        ecsr->emplace<ecs::comp::Node>(node, ecs::comp::Node{ .name = fgltfnode.name.c_str(),
                                                              .meshes = fgltfnode.meshIndex ? hmeshes.at(*fgltfnode.meshIndex)
                                                                                            : std::vector<Handle<gfx::Mesh>>{} });
        bnodes.push_back(node);
    }
    fastgltf::iterateSceneNodes(
        fgltfa, 0ull, fastgltf::math::fmat4x4{},
        [this, &ecsr, &fgltfa, &bnodes](fastgltf::Node& gltfn, const fastgltf::math::fmat4x4& fgltfgt) {
            const auto fgltfi = std::distance(fgltfa.nodes.data(), &gltfn);
            const auto node = bnodes.at(fgltfi);
            const auto& trs = std::get<fastgltf::TRS>(gltfn.transform);
            const auto glm_local =
                glm::translate(glm::mat4{ 1.0f }, glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() }) *
                glm::mat4_cast(glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() }) *
                glm::scale(glm::mat4{ 1.0f }, glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
            auto glm_global = glm::mat4{ 1.0f };
            memcpy(&glm_global, &fgltfgt, sizeof(fgltfgt));
            ecsr->emplace<ecs::comp::Transform>(node, glm_local, glm_global);
            for(const auto ci : gltfn.children)
            {
                ecsr->make_child(node, bnodes.at(ci));
            }
        });
    for(const auto ni : fgltfs.nodeIndices)
    {
        ecsr->make_child(ecsroot, bnodes.at(ni));
    }

    nodes[filepath] = ecsroot;
    return ecsroot;
}

ecs::Entity Scene::instance_entity(ecs::Entity node)
{
    auto* ecs = Engine::get().ecs;
    auto* r = Engine::get().renderer;
    std::optional<ecs::Entity> root;
    ecs->traverse_hierarchy(node, [ecs, r, &root](auto e) {
        auto ce = ecs->create();
        if(!root) { root = ce; }
        const auto& t = *ecs->get<ecs::comp::Transform>(e);
        ecs->emplace<ecs::comp::Transform>(ce, t);
        if(ecs->has<ecs::comp::Node>(e))
        {
            auto* cn = ecs->emplace<ecs::comp::Node>(ce);
            auto* n = ecs->get<ecs::comp::Node>(e);
            cn->name = n->name;
            cn->meshes.reserve(n->meshes.size());
            for(const auto& e : n->meshes)
            {
                cn->meshes.push_back(r->instance_mesh(gfx::InstanceSettings{ .mesh = e }));
            }
        }
    });
    scene.push_back(*root);
    return *root;
}
//{
//    if(!model) { return {}; }
//    auto& m = models.at(model);
//    if(!m.root_node) { return {}; }
//
//    const auto traverse_nodes = [this](Handle<Node> nh, const auto& self) -> Handle<NodeInstance> {
//        NodeInstance ni;
//        const Node& n = nodes.at(nh);
//        ni.name = n.name;
//        ni.children.reserve(n.children.size());
//        ni.transform = n.transform;
//        ni.entity = Engine::get().ecs->create();
//        Engine::get().ecs->emplace<components::Transform>(ni.entity)->transform = n.final_transform;
//
//        if(n.mesh)
//        {
//            auto& nm = meshes.at(n.mesh);
//            auto& nim = *Engine::get().ecs->emplace<components::Mesh>(ni.entity);
//            nim.submeshes.reserve(nm.submeshes.size());
//            for(const auto& nsm : nm.submeshes)
//            {
//                nim.submeshes.push_back(nsm.gfx_handle);
//            }
//            Engine::get().renderer->instance_entity(gfx::InstanceSettings{ .entity = ni.entity });
//        }
//
//        for(const auto& nch : n.children)
//        {
//            ni.children.push_back(self(nch, self));
//        }
//        const auto nih = node_instances.insert(std::move(ni));
//        return nih;
//    };
//
//    const auto root_node_instance = traverse_nodes(m.root_node, traverse_nodes);
//    ModelInstance mi{ .root_node = root_node_instance };
//    scene.push_back(mi);
//}

} // namespace scene
