#include <stack>
#include <ranges>
#include <filesystem>
#include <unordered_map>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb/stb_image.h>
#include <eng/scene.hpp>
#include <eng/engine.hpp>
#include <eng/ecs/components.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/paths.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/camera.hpp>
#include <eng/physics/bvh.hpp>
#include <third_party/imgui/imgui.h>
#include <third_party/imgui/imgui_internal.h>
#include <third_party/ImGuizmo/ImGuizmo.h>

namespace eng
{

namespace gltf
{

struct Context
{
    std::vector<uint32_t> images;
    std::vector<uint32_t> textures;
    std::vector<Range32u> materials;
    std::vector<Range32u> geometries;
    std::vector<Range32u> meshes;
};

Range32u load_geometry(Scene& scene, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Context& ctx)
{
    if(ctx.geometries.empty()) { ctx.geometries.insert(ctx.geometries.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.geometries[gltfmeshidx].size != 0u) { return ctx.geometries[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];

    Range32u geoms{ (uint32_t)scene.geometries.size(), 0u };
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
                float v[num_floats]{};
                for(auto i = 0u; i < num_floats; ++i)
                {
                    v[i] = vec[i];
                }
                auto* pdst = get_vertex_component(idx, gfxcomp);
                memcpy(pdst, v, sizeof(v));
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

        scene.geometries.push_back(ecs::Geometry{
            .render_geometry = Engine::get().renderer->make_geometry(gfx::GeometryDescriptor{
                .flags = {}, .vertex_layout = vertex_layout, .vertices = vertices, .indices = std::span{ indices } }) });
        ++geoms.size;
        // auto& geom = model.geometries.emplace_back();
        // geom.render_geometry = render_geometry;
        // geom.bvh = physics::BVH{ std::as_bytes(std::span{ vertices }), gfx::get_vertex_layout_size(vertex_layout),
        //                          std::as_bytes(std::span{ indices }), gfx::IndexFormat::U32 };
    }

    ctx.geometries[gltfmeshidx] = geoms;
    return geoms;
}

uint32_t load_image(Scene& scene, const fastgltf::Asset& gltfasset, gfx::ImageFormat format, size_t gltfimgidx, Context& ctx)
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

    const auto img = Engine::get().renderer->make_image(gltfimg.name.c_str(),
                                                        gfx::Image::init((uint32_t)x, (uint32_t)y, 0, format,
                                                                         gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                             gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                         0, 1, gfx::ImageLayout::READ_ONLY));
    if(!img) { ENG_ERROR("Failed to create image{}", gltfimg.name.c_str()); }
    else
    {
        gfx::get_renderer().staging->copy(img, imgdata, 0, 0, true, gfx::DiscardContents::YES);
        ENG_TODO("TODO: Process mips");
    }
    stbi_image_free(imgdata);
    if(!img) { return ~0u; }

    const uint32_t imgidx = scene.images.size();
    scene.images.push_back(img);
    ctx.images[gltfimgidx] = imgidx;
    return imgidx;
}

uint32_t load_texture(Scene& scene, const fastgltf::Asset& gltfasset, gfx::ImageFormat format, size_t gltftexidx, Context& ctx)
{
    if(ctx.textures.empty()) { ctx.textures.insert(ctx.textures.begin(), gltfasset.textures.size(), ~0u); }
    if(ctx.textures[gltftexidx] != ~0u) { return ctx.textures[gltftexidx]; }
    const auto& gltftex = gltfasset.textures[gltftexidx];
    if(!gltftex.imageIndex)
    {
        ENG_ERROR("Texture {} does not have associated image", gltftex.name.c_str());
        return ~0u;
    }
    uint32_t image = load_image(scene, gltfasset, format, *gltftex.imageIndex, ctx);
    if(image == ~0u) { return ~0u; }

    uint32_t texidx = scene.textures.size();
    scene.textures.push_back(gfx::ImageView::init(scene.images[image], format));
    ctx.textures[gltftexidx] = texidx;
    return texidx;
}

Range32u load_material(Scene& scene, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, Context& ctx)
{
    if(ctx.materials.empty()) { ctx.materials.insert(ctx.materials.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.materials[gltfmeshidx].size != 0u) { return ctx.materials[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];
    Range32u mats{ (uint32_t)scene.materials.size(), 0u };
    for(auto i = 0ull; i < gltfmesh.primitives.size(); ++i)
    {
        const fastgltf::Primitive& gltfprim = gltfmesh.primitives[i];

        if(!gltfprim.materialIndex)
        {
            ENG_ERROR("Primitive {} in mesh {} does not have a material", gltfmesh.name.c_str(), i);
            continue;
        }

        const fastgltf::Material& gltfmat = gltfasset.materials[*gltfprim.materialIndex];

        auto matdesc = gfx::MaterialDescriptor{};
        if(gltfmat.pbrData.baseColorTexture)
        {
            uint32_t texidx = load_texture(scene, gltfasset, gfx::ImageFormat::R8G8B8A8_SRGB,
                                           gltfmat.pbrData.baseColorTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                matdesc.base_color_texture = scene.textures[gltfmat.pbrData.baseColorTexture->textureIndex];
            }
        }
        if(gltfmat.normalTexture)
        {
            uint32_t texidx =
                load_texture(scene, gltfasset, gfx::ImageFormat::R8G8B8A8_UNORM, gltfmat.normalTexture->textureIndex, ctx);
            if(texidx != ~0u) { matdesc.normal_texture = scene.textures[gltfmat.normalTexture->textureIndex]; }
        }
        if(gltfmat.pbrData.metallicRoughnessTexture)
        {
            uint32_t texidx = load_texture(scene, gltfasset, gfx::ImageFormat::R8G8B8A8_UNORM,
                                           gltfmat.pbrData.metallicRoughnessTexture->textureIndex, ctx);
            if(texidx != ~0u)
            {
                matdesc.metallic_roughness_texture = scene.textures[gltfmat.pbrData.metallicRoughnessTexture->textureIndex];
            }
        }

        scene.materials.push_back(ecs::Material{
            .name = gltfmat.name.c_str(),
            .render_material = Engine::get().renderer->make_material(matdesc),
        });
        ++mats.size;
    }

    ctx.materials[gltfmeshidx] = mats;
    return mats;
}

Range32u load_mesh(Scene& scene, const fastgltf::Asset& gltfasset, size_t gltfmeshidx, SceneNode& node, Context& ctx)
{
    if(ctx.meshes.empty()) { ctx.meshes.insert(ctx.meshes.begin(), gltfasset.meshes.size(), Range32u{}); }
    if(ctx.meshes[gltfmeshidx].size != 0u) { return ctx.meshes[gltfmeshidx]; }

    const fastgltf::Mesh& gltfmesh = gltfasset.meshes[gltfmeshidx];

    Range32u geoms = load_geometry(scene, gltfasset, gltfmeshidx, ctx);
    Range32u mats = load_material(scene, gltfasset, gltfmeshidx, ctx);
    ENG_ASSERT(geoms == mats && geoms.offset == (uint32_t)scene.meshes.size());
    for(auto i = 0u; i < geoms.size; ++i)
    {
        scene.meshes.push_back(gfx::get_renderer().make_mesh(gfx::MeshDescriptor{
            .geometry = scene.geometries[geoms.offset + i].render_geometry, .material = scene.materials[mats.offset + i].render_material }));
    }
    ctx.meshes[gltfmeshidx] = geoms;
    return geoms;
}

SceneNodeId load_node(Scene& scene, const fastgltf::Asset& gltfasset, uint32_t gltfnodeidx, SceneNodeId parentnodeid, Context& ctx)
{
    const auto& gltfnode = gltfasset.nodes[gltfnodeidx];
    const auto nodeid = scene.make_node(gltfnode.name.c_str());
    scene.hierarchy.make_child(parentnodeid, nodeid);

    auto& node = scene.get_node(nodeid);
    node.transform = scene.transforms.size();
    auto& transform = scene.transforms.emplace_back(scene.transforms[scene.get_node(parentnodeid).transform]);

    if(gltfnode.transform.index() == 0)
    {
        const auto& gltftrs = std::get<fastgltf::TRS>(gltfnode.transform);
        ecs::Transform trs{ .position = glm::vec3{ gltftrs.translation.x(), gltftrs.translation.y(), gltftrs.translation.z() },
                            .rotation = glm::quat{ gltftrs.rotation.w(), gltftrs.rotation.x(), gltftrs.rotation.y(),
                                                   gltftrs.rotation.z() },
                            .scale = glm::vec3{ gltftrs.scale.x(), gltftrs.scale.y(), gltftrs.scale.z() } };
        transform = ecs::Transform::init(trs.to_mat4() * transform.to_mat4());
    }
    else
    {
        const auto& trs = std::get<fastgltf::math::fmat4x4>(gltfnode.transform);
        glm::mat4 glmtrs;
        memcpy(&glmtrs, &trs, sizeof(trs));
        transform = ecs::Transform::init(glmtrs * transform.to_mat4());
    }

    if(gltfnode.meshIndex)
    {
        node.meshes = load_mesh(scene, gltfasset, *gltfnode.meshIndex, node, ctx);
        if(node.meshes.size == 0u)
        {
            ENG_ERROR("Failed to load mesh {} for node {}.", gltfasset.meshes.at(*gltfnode.meshIndex).name.c_str(),
                      gltfnode.name.c_str());
        }
    }

    for(const auto& e : gltfnode.children)
    {
        load_node(scene, gltfasset, e, nodeid, ctx);
    }

    return nodeid;
}

SceneNodeId load_from_file(Scene& scene, const std::filesystem::path& model_path)
{
    auto fastdatabuf = fastgltf::GltfDataBuffer::FromPath(model_path);
    if(!fastdatabuf) { return SceneNodeId{}; }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    fastgltf::Parser gltfparser;
    auto gltfassetexp = gltfparser.loadGltfBinary(fastdatabuf.get(), model_path.parent_path(), gltfOptions);
    if(!gltfassetexp) { return SceneNodeId{}; }

    auto& gltfasset = gltfassetexp.get();
    if(gltfasset.scenes.empty()) { return SceneNodeId{}; }

    auto& gltfscene = gltfasset.scenes.at(0);

    const auto rootnodeid = scene.make_node(model_path.filename().string());
    scene.get_node(rootnodeid).transform = scene.transforms.size();
    scene.transforms.emplace_back();

    Context ctx;
    for(auto i = 0u; i < gltfscene.nodeIndices.size(); ++i)
    {
        load_node(scene, gltfasset, gltfscene.nodeIndices[i], rootnodeid, ctx);
    }

    return rootnodeid;
}

} // namespace gltf

void Scene::init() {}

SceneNodeId Scene::load_from_file(const std::filesystem::path& model_path)
{
    auto full_path = model_path;
    if(!full_path.string().starts_with(eng::paths::MODELS_DIR.string()))
    {
        full_path = eng::paths::MODELS_DIR / full_path;
    }

    const auto ext = full_path.extension();
    if(ext == ".glb") { return gltf::load_from_file(*this, full_path); }

    ENG_ERROR("Extension not supported {}", ext.string());
    return SceneNodeId{};
}

ecs::EntityId Scene::instance_model(SceneNodeId nodeid)
{
    if(!hierarchy.has(nodeid))
    {
        ENG_ERROR("Cannot instatiate node {}", *nodeid);
        return ecs::EntityId{};
    }
    auto* reg = Engine::get().ecs;
    ecs::EntityId ret;
    std::unordered_map<SceneNodeId, ecs::EntityId> parents;
    hierarchy.traverse_hierarchy(nodeid, [this, reg, &ret, &parents](IndexedHierarchy::NodeId nodeid) {
        const SceneNodeId sceneid{ *nodeid };
        const SceneNode& node = get_node(sceneid);
        ENG_ASSERT(node.transform != ~0u);
        const auto e = reg->create();
        if(!ret) { ret = e; }
        reg->add_components(e, transforms[node.transform]);
        if(node.meshes.size != 0)
        {
            ecs::Mesh ecsmesh{ .render_meshes = { meshes.begin() + node.meshes.offset,
                                                  meshes.begin() + node.meshes.offset + node.meshes.size } };
            reg->add_components(e, ecsmesh);
        }

        auto parentid = hierarchy.get_parent(nodeid);
        if(hierarchy.has(parentid)) { reg->make_child(parents[SceneNodeId{ *parentid }], e); }
        parents[sceneid] = e;
    });
    ENG_ASSERT(ret);
    return ret;
}

SceneNodeId Scene::make_node(const std::string& name)
{
    const auto idx = hierarchy.create();
    if(!idx)
    {
        ENG_ERROR("Could not create new scene node");
        return SceneNodeId{ *idx };
    }
    if(nodes.size() == *idx) { nodes.push_back(SceneNode{ .name = name }); }
    return SceneNodeId{ *idx };
}

SceneNode& Scene::get_node(SceneNodeId id)
{
    if(!hierarchy.has(id))
    {
        ENG_ERROR("Invalid node id");
        return null_node;
    }
    return nodes[*id];
}

} // namespace eng

#if 0

ecs::EntityId Scene::instance_model(const asset::Model* model)
{
    if(!model) { return ecs::EntityId{}; }

    static constexpr auto make_hierarchy = [](const auto& self, const asset::Model& model,
                                              const asset::Model::Node& node, ecs::EntityId parent) -> ecs::EntityId {
        auto* ecsr = Engine::get().ecs;
        auto entity = ecsr->create();
        ecsr->add_components(entity, ecs::Node{ node.name, &model }, ecs::Transform{ glm::mat4{ 1.0f }, node.transform });
        if(node.mesh != ~0u) { ecsr->add_components(entity, ecs::Mesh{ &model.meshes.at(node.mesh), ~0u }); }
        if(parent) { ecsr->make_child(parent, entity); }
        for(const auto& e : node.children)
        {
            self(self, model, model.nodes.at(e), entity);
        }
        return entity;
    };

    const auto instance = make_hierarchy(make_hierarchy, *model, model->nodes.at(model->root_node), ecs::EntityId{});
    scene.push_back(instance);
    return instance;
}

void Scene::update_transform(ecs::EntityId entity)
{
    auto& ecstrs = Engine::get().ecs->get<ecs::Transform>(entity);
    pending_transforms.push_back(entity);
}

void Scene::update()
{
    // Relies on pending transforms not having child nodes of other nodes (no two nodes from the same hierarchy)
    if(pending_transforms.size())
    {
        std::unordered_set<ecs::EntityId> visited;

        // leave only those entities, who have no ancestors in the pending trs.
        std::vector<ecs::EntityId> filtered;
        filtered.reserve(pending_transforms.size());
        visited.insert(pending_transforms.begin(), pending_transforms.end());
        for(auto e : pending_transforms)
        {
            auto p = e;
            auto passes = true;
            while(p)
            {
                p = Engine::get().ecs->get_parent(p);
                if(visited.contains(p))
                {
                    passes = false;
                    break;
                }
            }
            if(passes) { filtered.push_back(e); }
        }
        pending_transforms = std::move(filtered);

        for(auto e : pending_transforms)
        {
            const auto p = Engine::get().ecs->get_parent(e);
            std::stack<ecs::EntityId> visit;
            std::stack<glm::mat4> trs;
            visit.push(e);
            if(p)
            {
                auto& pt = Engine::get().ecs->get<ecs::Transform>(p);
                trs.push(pt.global);
            }
            else { trs.push(glm::identity<glm::mat4>()); }

            while(visit.size())
            {
                ENG_ASSERT(trs.size() == visit.size());
                auto e = visit.top();
                auto pt = trs.top();
                visit.pop();
                trs.pop();
                auto& t = Engine::get().ecs->get<ecs::Transform>(e);
                t.global = t.local * pt;

                ENG_ASSERT(false);
                // Engine::get().renderer->update_transform(e);
                Engine::get().ecs->loop_over_children(e, [&t, &trs, &visit](auto e) {
                    trs.push(t.global);
                    visit.push(e);
                });
            }
        }
        pending_transforms.clear();
    }
}

void Scene::ui_draw_scene()
{
    const auto expand_hierarchy = [this](ecs::Registry* reg, ecs::EntityId e, bool expand, const auto& self) -> void {
        ui.scene.nodes[e].expanded = expand;
        reg->loop_over_children(e, [&](ecs::EntityId ch) { self(reg, ch, expand, self); });
    };

    const auto draw_hierarchy = [&, this](ecs::Registry* reg, ecs::EntityId e, const auto& self) -> void {
        const auto enode = reg->get<ecs::Node>(e);
        ImGui::PushID((int)*e);
        auto& ui_node = ui.scene.nodes[e];
        // ImGui::BeginGroup();
        if(reg->has_children(e))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImGui::GetStyle().ItemSpacing * 0.5f);
            if(ImGui::ArrowButton("expand_btn", ui_node.expanded ? ImGuiDir_Down : ImGuiDir_Right))
            {
                ui_node.expanded = !ui_node.expanded;
            }
            ImGui::PopStyleVar(1);
            ImGui::SameLine();
        }
        {
            bool is_sel = e == ui.scene.sel_entity;
            auto cpos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(cpos + ImVec2{ -ImGui::GetStyle().ItemSpacing.x * 0.5f, 0.0f });
            ImGui::GetItemRectSize();
            if(ImGui::Selectable(enode.name.c_str(), &is_sel)) { ui.scene.sel_entity = e; }
        }
        // ImGui::EndGroup();
        if(ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(0))
        {
            expand_hierarchy(reg, e, !ui_node.expanded, expand_hierarchy);
        }

        if(ui_node.expanded)
        {
            ImGui::Indent();
            reg->loop_over_children(e, [&](ecs::EntityId ec) { self(reg, ec, self); });
            ImGui::Unindent();
        }
        ImGui::PopID();
    };
    for(const auto& e : scene)
    {
        draw_hierarchy(Engine::get().ecs, e, draw_hierarchy);
    }
}

void Scene::ui_draw_inspector()
{
    if(!ui.scene.sel_entity) { return; }

    auto* ecs = Engine::get().ecs;
    auto& entity = ui.scene.sel_entity;
    auto& uie = ui.scene.nodes.at(entity);
    auto& ctransform = ecs->get<ecs::Transform>(entity);
    auto& cnode = ecs->get<ecs::Node>(entity);
    auto& cmesh = ecs->get<ecs::Mesh>(entity);
    auto& clight = ecs->get<ecs::Light>(entity);

    ENG_ASSERT(false);

    // if(ImGui::Begin("Inspector", 0, ImGuiWindowFlags_HorizontalScrollbar))
    //{
    //     ENG_ASSERT(cnode && ctransform);
    //     ImGui::SeparatorText("Node");
    //     ImGui::SeparatorText("Transform");
    //     if(ImGui::DragFloat3("Position", &ctransform->local[3].x)) { update_transform(entity); }
    //     if(cmesh)
    //     {
    //         ImGui::SeparatorText("Mesh renderer");
    //         ImGui::Text(cmesh->mesh->name.c_str());
    //         if(cmesh->meshes.size())
    //         {
    //             // ImGui::Indent();
    //             for(auto& e : cmesh->meshes)
    //             {
    //                 auto& material = e->material.get();
    //                 ImGui::Text("Pass: %s", material.mesh_pass->name.c_str());
    //                 if(material.base_color_texture)
    //                 {
    //                     ImGui::Image(*material.base_color_texture + 1, { 128.0f, 128.0f });
    //                 }
    //             }
    //             // ImGui::Unindent();
    //         }
    //     }
    //     if(clight)
    //     {
    //         ImGui::SeparatorText("Light");
    //         ImGui::Text("Type: %s", to_string(clight->type).c_str());
    //         bool edited = false;
    //         edited |= ImGui::ColorEdit4("Color", &clight->color.x);
    //         edited |= ImGui::SliderFloat("Range", &clight->range, 0.0f, 100.0f);
    //         edited |= ImGui::SliderFloat("Intensity", &clight->intensity, 0.0f, 100.0f);
    //         // todo: don't like that entities with light component have to be detected and handled separately
    //         if(edited) { update_transform(entity); }
    //     }

    //    if(cmesh)
    //    {
    //        ImGui::SeparatorText("BVH");
    //        for(auto i = 0u; i < cmesh->mesh->geometries.size; ++i)
    //        {
    //            const auto& bvh = cnode->model->geometries[cmesh->mesh->geometries.offset + i].bvh;
    //            const auto stats = bvh.get_stats();
    //            ImGui::Checkbox("##bvh_level_exclusive", &uie.bvh_level_exclusive);
    //            ImGui::SameLine();
    //            if(ImGui::IsItemHovered()) { ImGui::SetItemTooltip("Shows levels up to X or only equal to X."); }
    //            ImGui::SliderInt("show level", &uie.bvh_level, 0, stats.levels);
    //            if(uie.bvh_level > 0)
    //            {
    //                for(auto ni = 0u; ni < stats.nodes.size(); ++ni)
    //                {
    //                    if((uie.bvh_level_exclusive && stats.metadatas[ni].level != uie.bvh_level) ||
    //                       (!uie.bvh_level_exclusive && stats.metadatas[ni].level > uie.bvh_level))
    //                    {
    //                        continue;
    //                    }
    //                    const auto& e = stats.nodes[ni];
    //                    Engine::get().renderer->debug_bufs.add(gfx::DebugGeometry::init_aabb(e.aabb.min, e.aabb.max));
    //                }
    //            }

    //            ImGui::Text("BVH%u: size[kB]: %llu, tris: %u, nodes: %u", i, stats.size / 1024,
    //                        (uint32_t)stats.tris.size(), (uint32_t)stats.nodes.size());
    //            const auto aabb = stats.nodes[0].aabb;
    //            ImGui::Text("\tExtent:");
    //            ImGui::Text("\t[%5.2f %5.2f %5.2f]", aabb.min.x, aabb.min.y, aabb.min.z);
    //            ImGui::Text("\t[%5.2f %5.2f %5.2f]", aabb.max.x, aabb.max.y, aabb.max.z);
    //        }
    //    }
    //}
    // ImGui::End();
}

//void Scene::ui_draw_manipulate()
//{
//    if(!ui.scene.sel_entity) { return; }
//
//    auto* ecs = Engine::get().ecs;
//    auto& entity = ui.scene.sel_entity;
//    auto& ctransform = ecs->get<ecs::Transform>(entity);
//    auto& cnode = ecs->get<ecs::Node>(entity);
//    auto& cmesh = ecs->get<ecs::Mesh>(entity);
//
//    auto& io = ImGui::GetIO();
//    auto& style = ImGui::GetStyle();
//    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0u); // don't set no background, make host dock push style with no bg, and somehow it works -
//                                                  // the content window actually does not have the background
//    ImGui::Begin("Manipulate", 0, ImGuiWindowFlags_NoDecoration);
//    ImGui::PopStyleColor(1);
//    ImGuizmo::SetDrawlist();
//
//    const auto view = Engine::get().camera->get_view();
//    auto proj = Engine::get().camera->get_projection(); // imguizmo hates inf_revz_zo perspective matrix that i use (div by 0 because no far plane)
//    proj = glm::perspectiveFov(glm::radians(75.0f), Engine::get().window->width, Engine::get().window->height, 0.1f, 30.0f);
//    const auto window_width = ImGui::GetWindowWidth();
//    const auto window_height = ImGui::GetWindowHeight();
//    const auto window_pos = ImGui::GetWindowPos();
//    glm::mat4 tr{ 1.0f };
//    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
//    auto translation = ctransform.global;
//    glm::mat4 delta;
//    if(ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::LOCAL,
//                            &ctransform.local[0][0]))
//    {
//        update_transform(entity);
//    }
//
//    ImGui::End();
//}
} // namespace eng

#endif