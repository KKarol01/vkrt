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
#include <meshoptimizer/src/meshoptimizer.h>

namespace scene
{

Handle<Model> Scene::load_from_file(const std::filesystem::path& _path)
{
    const auto path = paths::canonize_path(_path, "models");

    if(path.extension() != ".glb")
    {
        assert(false && "Only .glb files are supported.");
        return {};
    }

    if(!std::filesystem::exists(path))
    {
        ENG_WARN("Path {} does not point to any file.", path.string());
        return {};
    }

    fastgltf::Parser fastparser;
    auto fastglbbuf = fastgltf::GltfDataBuffer::FromPath(path);
    if(!fastglbbuf)
    {
        ENG_WARN("Error during fastgltf::GltfDataBuffer import: {}", fastgltf::getErrorName(fastglbbuf.error()));
        return {};
    }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    auto fastgltfastassetexpected = fastparser.loadGltfBinary(fastglbbuf.get(), path.parent_path(), gltfOptions);
    if(!fastgltfastassetexpected)
    {
        ENG_WARN("Error during loading fastgltf::Parser::loadGltfBinary: {}",
                 fastgltf::getErrorName(fastgltfastassetexpected.error()));
        return {};
    }

    auto& fastasset = fastgltfastassetexpected.get();
    if(fastasset.scenes.empty())
    {
        ENG_WARN("Error during loading. Fastgltf asset does not have any scenes defined.");
        return {};
    }

    auto& fastscene = fastasset.scenes.at(0);
    Model model{ .path = path, .root_node = nodes.emplace(Node{ .name = fastscene.name.c_str() }) };
    Node& root_node = nodes.at(model.root_node);
    std::vector<Handle<Image>> batched_images;
    std::vector<Handle<Texture>> batched_textures;
    std::vector<Handle<Material>> batched_materials;
    std::vector<Handle<Mesh>> batched_meshes;
    std::vector<Handle<Node>> batched_nodes;
    batched_images.reserve(fastasset.images.size());
    batched_textures.reserve(fastasset.textures.size());
    batched_materials.reserve(fastasset.materials.size());
    batched_meshes.reserve(fastasset.meshes.size());
    batched_nodes.reserve(fastasset.nodes.size());

    for(auto i = 0u; i < fastasset.images.size(); ++i)
    {
        using namespace fastgltf;
        Image image;
        auto& fastimage = fastasset.images.at(i);
        std::span<const std::byte> data;
        if(auto fastsrcbview = std::get_if<sources::BufferView>(&fastimage.data))
        {
            auto& fastbview = fastasset.bufferViews.at(fastsrcbview->bufferViewIndex);
            auto& fastbuf = fastasset.buffers.at(fastbview.bufferIndex);
            if(auto fastsrcarr = std::get_if<sources::Array>(&fastbuf.data))
            {
                data = { fastsrcarr->bytes.data() + fastbview.byteOffset, fastbview.byteLength };
            }
        }
        if(!data.empty())
        {
            image.name = fastimage.name.c_str();
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
                stbi_image_free(imgdata);
            }
        }
        else { ENG_WARN("Could not load image {}", fastimage.name.c_str()); }
        batched_images.push_back(images.emplace(std::move(image)));
    }

    for(auto i = 0u; i < fastasset.textures.size(); ++i)
    {
        auto& fasttxt = fastasset.textures.at(i);
        if(!fasttxt.imageIndex)
        {
            ENG_WARN("Unsupported texture {} type.", fasttxt.name.c_str());
            batched_textures.push_back(textures.emplace());
        }
        else
        {
            Texture txt;
            if(fasttxt.samplerIndex)
            {
                auto& fsamp = fastasset.samplers.at(*fasttxt.samplerIndex);
                ENG_TODO("Implement sampler settings import from fastgltf");
            }
            txt.image = batched_images.at(*fasttxt.imageIndex);
            batched_textures.push_back(textures.emplace(std::move(txt)));
        }
    }

    for(auto i = 0u; i < fastasset.materials.size(); ++i)
    {
        auto& fastmat = fastasset.materials.at(i);
        Material mat;
        mat.name = fastmat.name.c_str();
        if(fastmat.pbrData.baseColorTexture)
        {
            assert(fastmat.pbrData.baseColorTexture->texCoordIndex == 0);
            const auto txtcolh = batched_textures.at(fastmat.pbrData.baseColorTexture->textureIndex);
            auto& txt = textures.at(txtcolh);
            auto& img = images.at(txt.image);
            img.format = gfx::ImageFormat::R8G8B8A8_SRGB;
            mat.base_color_texture = txtcolh;
        }
        batched_materials.push_back(materials.emplace(std::move(mat)));
    }

    for(auto i = 0u; i < fastasset.meshes.size(); ++i)
    {
        auto& fmesh = fastasset.meshes.at(i);
        Mesh mesh;
        mesh.name = fmesh.name.c_str();
        mesh.submeshes.reserve(fmesh.primitives.size());
        for(auto j = 0u; j < fmesh.primitives.size(); ++j)
        {
            const auto load_primitive = [&]() {
                Submesh submesh;
                auto& fprim = fmesh.primitives.at(j);
                std::vector<gfx::Vertex> vertices;
                std::vector<gfx::Index32> indices;
                if(auto it = fprim.findAttribute("POSITION"); it != fprim.attributes.end())
                {
                    auto& acc = fastasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    vertices.resize(acc.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fastasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).position = { vec.x(), vec.y(), vec.z() };
                    });
                }
                else
                {
                    ENG_WARN("Mesh primitive does not contain position. Skipping...");
                    return submesh;
                }
                if(auto it = fprim.findAttribute("NORMAL"); it != fprim.attributes.end())
                {
                    auto& acc = fastasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fastasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).normal = { vec.x(), vec.y(), vec.z() };
                    });
                }
                if(auto it = fprim.findAttribute("TEXCOORD_0"); it != fprim.attributes.end())
                {
                    auto& acc = fastasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(fastasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).uv = { vec.x(), vec.y() };
                    });
                }
                if(auto it = fprim.findAttribute("TANGENT"); it != fprim.attributes.end())
                {
                    auto& acc = fastasset.accessors.at(it->accessorIndex);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(fastasset, acc, [&vertices](const auto& vec, auto idx) {
                        vertices.at(idx).tangent = { vec.x(), vec.y(), vec.z(), vec.w() };
                    });
                }
                if(!fprim.indicesAccessor)
                {
                    ENG_WARN("Mesh primitive {}:{} does not have mandatory vertex indices. Skipping...", fmesh.name.c_str(), j);
                    return submesh;
                }
                else
                {
                    auto& acc = fastasset.accessors.at(*fprim.indicesAccessor);
                    if(!acc.bufferViewIndex)
                    {
                        ENG_ERROR("No bufferViewIndex...");
                        return submesh;
                    }
                    indices.resize(acc.count);
                    fastgltf::copyFromAccessor<uint32_t>(fastasset, acc, indices.data());
                }
                if(fprim.materialIndex) { submesh.material = batched_materials.at(*fprim.materialIndex); }
                else { ENG_WARN("Submesh #{} in mesh {} does not have a material", fmesh.name.c_str(), j); }

                submesh.geometry = geometries.emplace(Geometry{
                    .vertices = std::move(vertices),
                    .indices = std::move(indices),
                });
                return submesh;
            };
            mesh.submeshes.push_back(load_primitive());
        }
        batched_meshes.push_back(meshes.emplace(std::move(mesh)));
    }

    for(auto i = 0u; i < fastasset.nodes.size(); ++i)
    {
        const auto& fastnode = fastasset.nodes.at(i);
        batched_nodes.push_back(nodes.emplace(Node{
            .name = fastnode.name.c_str(), .mesh = fastnode.meshIndex ? batched_meshes.at(*fastnode.meshIndex) : Handle<Mesh>{} }));
    }
    fastgltf::iterateSceneNodes(
        fastasset, 0ull, fastgltf::math::fmat4x4{},
        [this, &fastasset, &batched_nodes](fastgltf::Node& fastnode, const fastgltf::math::fmat4x4& fasttransform) {
            const auto fastnodeindex = &fastnode - fastasset.nodes.data();
            auto& node = nodes.at(batched_nodes.at(fastnodeindex));
            node.children.resize(fastnode.children.size());
            std::transform(fastnode.children.begin(), fastnode.children.end(), node.children.begin(),
                           [&batched_nodes](size_t idx) { return batched_nodes.at(idx); });

            const auto& trs = std::get<fastgltf::TRS>(fastnode.transform);
            const auto glmtrs =
                glm::translate(glm::mat4{ 1.0f }, glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() }) *
                glm::mat4_cast(glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() }) *
                glm::scale(glm::mat4{ 1.0f }, glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
            node.transform = glmtrs;
            memcpy(&node.final_transform, &fasttransform, sizeof(fasttransform));
        });
    root_node.children.resize(fastscene.nodeIndices.size());
    std::transform(fastscene.nodeIndices.begin(), fastscene.nodeIndices.end(), root_node.children.begin(),
                   [&batched_nodes](size_t idx) { return batched_nodes.at(idx); });

    for(const auto bih : batched_images)
    {
        auto& i = images.at(bih);
        i.gfx_handle = Engine::get().renderer->batch_image(gfx::ImageDescriptor{ .name = i.name,
                                                                                 .width = i.width,
                                                                                 .height = i.height,
                                                                                 .depth = i.depth,
                                                                                 .mips = 1,
                                                                                 .format = i.format,
                                                                                 .type = gfx::ImageType::TYPE_2D,
                                                                                 .data = i.data });
        i.data.clear();
    }

    for(const auto bth : batched_textures)
    {
        auto& t = textures.at(bth);
        auto& i = images.at(t.image);
        t.gfx_handle = Engine::get().renderer->batch_texture(gfx::TextureDescriptor{
            .image = i.gfx_handle, .filtering = t.filtering, .addressing = t.addressing });
    }

    for(const auto bmh : batched_materials)
    {
        auto& m = materials.at(bmh);
        m.gfx_handle = Engine::get().renderer->batch_material(gfx::MaterialDescriptor{
            .base_color_texture = textures.at(m.base_color_texture).gfx_handle });
    }

    for(const auto bmh : batched_meshes)
    {
        auto& m = meshes.at(bmh);
        for(auto& sm : m.submeshes)
        {
            static constexpr auto max_verts = 64u;
            static constexpr auto max_tris = 124u;
            static constexpr auto cone_weight = 0.0f;
            auto& g = geometries.at(sm.geometry);

            const auto max_meshlets = meshopt_buildMeshletsBound(g.indices.size(), max_verts, max_tris);
            std::vector<meshopt_Meshlet> meshlets(max_meshlets);
            std::vector<meshopt_Bounds> meshlets_bounds;
            std::vector<uint32_t> meshlets_verts(max_meshlets * max_verts);
            std::vector<uint8_t> meshlets_triangles(max_meshlets * max_tris * 3);

            const auto meshlet_count =
                meshopt_buildMeshlets(meshlets.data(), meshlets_verts.data(), meshlets_triangles.data(),
                                      g.indices.data(), g.indices.size(), &g.vertices.at(0).position.x,
                                      g.vertices.size(), sizeof(g.vertices.at(0)), max_verts, max_tris, cone_weight);

            const auto& last_meshlet = meshlets.at(meshlet_count - 1);
            meshlets_verts.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
            meshlets_triangles.resize(last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3));
            meshlets.resize(meshlet_count);
            meshlets_bounds.reserve(meshlet_count);

            for(auto& m : meshlets)
            {
                meshopt_optimizeMeshlet(&meshlets_verts.at(m.vertex_offset), &meshlets_triangles.at(m.triangle_offset),
                                        m.triangle_count, m.vertex_count);
                const auto mbounds = meshopt_computeMeshletBounds(&meshlets_verts.at(m.vertex_offset),
                                                                  &meshlets_triangles.at(m.triangle_offset),
                                                                  m.triangle_count, &g.vertices.at(0).position.x,
                                                                  g.vertices.size(), sizeof(g.vertices.at(0)));
                meshlets_bounds.push_back(mbounds);
            }

            std::vector<gfx::Vertex> final_vertices;
            final_vertices.resize(meshlets_verts.size());
            std::transform(meshlets_verts.begin(), meshlets_verts.end(), final_vertices.begin(),
                           [&g](uint32_t idx) { return g.vertices.at(idx); });
            g.vertices = std::move(final_vertices);
            g.indices.clear();
            g.indices.reserve(meshlets_triangles.size());
            std::transform(meshlets_triangles.begin(), meshlets_triangles.end(), std::back_inserter(g.indices),
                           [](auto idx) { return gfx::Index32{ idx }; });
            g.meshlets.resize(meshlet_count);
            for(auto i = 0u; i < meshlet_count; ++i)
            {
                const auto& m = meshlets.at(i);
                const auto& mb = meshlets_bounds.at(i);
                g.meshlets.at(i) =
                    gfx::Meshlet{ .vertex_range = { m.vertex_offset, m.vertex_count },
                                  .triangle_range = { m.triangle_offset, m.triangle_count },
                                  .bounding_sphere = glm::vec4{ mb.center[0], mb.center[1], mb.center[2], mb.radius } };
            }

            g.gfx_handle = Engine::get().renderer->batch_geometry(gfx::GeometryDescriptor{
                .vertices = g.vertices, .indices = g.indices, .meshlets = g.meshlets });
            g.vertices.clear();
            g.indices.clear();
            g.meshlets.clear();

            auto* m = sm.material ? &materials.at(sm.material) : nullptr;
            sm.gfx_handle = Engine::get().renderer->batch_mesh(gfx::MeshDescriptor{
                .geometry = g.gfx_handle, .material = m ? m->gfx_handle : Handle<gfx::Material>{} });
        }
    }

    return models.emplace(std::move(model));
}

Handle<ModelInstance> Scene::instance_model(Handle<Model> model)
{
    if(!model) { return {}; }
    auto& m = models.at(model);
    if(!m.root_node) { return {}; }

    const auto traverse_nodes = [this](Handle<Node> nh, const auto& self) -> Handle<NodeInstance> {
        NodeInstance ni;
        const Node& n = nodes.at(nh);
        ni.name = n.name;
        ni.children.reserve(n.children.size());
        ni.transform = n.transform;
        ni.entity = Engine::get().ecs->create();
        Engine::get().ecs->emplace<components::Transform>(ni.entity)->transform = n.final_transform;

        if(n.mesh)
        {
            auto& nm = meshes.at(n.mesh);
            auto& nim = *Engine::get().ecs->emplace<components::Mesh>(ni.entity);
            nim.submeshes.reserve(nm.submeshes.size());
            for(const auto& nsm : nm.submeshes)
            {
                nim.submeshes.push_back(nsm.gfx_handle);
            }
            Engine::get().renderer->instance_mesh(gfx::InstanceSettings{ .entity = ni.entity });
        }

        for(const auto& nch : n.children)
        {
            ni.children.push_back(self(nch, self));
        }
        const auto nih = node_instances.insert(std::move(ni));
        return nih;
    };

    const auto root_node_instance = traverse_nodes(m.root_node, traverse_nodes);
    ModelInstance mi{ .root_node = root_node_instance };
    scene.push_back(mi);
}

void Scene::update_transform(Handle<NodeInstance> entity, glm::mat4 transform)
{
    // ENG_TODO();
    // const auto ent = instance_handles.at(entity);
    // Engine::get().ecs_system->get<components::Transform>(ent->entity).transform = transform;
    // for(auto& e : ent->children) {
    //     Engine::get().ecs_system->get<components::Transform>(instance_handles.at(e)->entity).transform = transform;
    // }
}

void Scene::upload_model_data(Handle<Model> model) {}

} // namespace scene
