#include <stack>
#include <ranges>
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
namespace asset
{
namespace import
{

asset::Model::Node& model_add_node(asset::Model& model, uint32_t* out_idx)
{
    if(out_idx) { *out_idx = model.nodes.size(); }
    return model.nodes.emplace_back();
}

namespace gltf
{

asset::Geometry* load_geometry(const fastgltf::Asset& fastasset, const fastgltf::Mesh& fastmesh,
                               uint32_t primitive_index, asset::Model& model)
{
    const auto& fprim = fastmesh.primitives.at(primitive_index);

    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    static constexpr const char* FAST_COMPS[]{ "POSITION", "NORMAL", "TANGENT", "TEXCOORD_0" };
    static constexpr gfx::VertexComponent GFX_COMPS[]{ gfx::VertexComponent::POSITION_BIT, gfx::VertexComponent::NORMAL_BIT,
                                                       gfx::VertexComponent::TANGENT_BIT, gfx::VertexComponent::UV0_BIT };

    const auto vertex_layout = [&fprim] {
        Flags<gfx::VertexComponent> vertex_layout{};
        for(auto i = 0u; i < std::size(FAST_COMPS); ++i)
        {
            if(fprim.findAttribute(FAST_COMPS[i]) != fprim.attributes.end()) { vertex_layout |= GFX_COMPS[i]; }
        }
        return vertex_layout;
    }();
    const auto vertex_size = gfx::get_vertex_layout_size(vertex_layout);
    const auto get_vertex_component = [&vertices, &vertex_size, &vertex_layout](size_t vidx, gfx::VertexComponent comp) -> std::byte* {
        auto* ptr = (std::byte*)vertices.data();
        return ptr + vertex_size * vidx + gfx::get_vertex_component_offset(vertex_layout, comp);
    };

    const auto fast_iterate = [&fastasset, &get_vertex_component](int comp, const auto& fastacc, gfx::VertexComponent gfxcomp) {
        const auto cb = [&get_vertex_component, &gfxcomp]<size_t comps>(const auto& vec, auto idx) {
            float v[comps]{};
            for(auto i = 0u; i < comps; ++i)
            {
                v[i] = vec[i];
            }
            auto* pdst = get_vertex_component(idx, gfxcomp);
            memcpy(pdst, v, gfx::get_vertex_component_size(gfxcomp));
        };
        const auto cb2 = [&cb](const auto& a, auto b) { cb.template operator()<2>(a, b); };
        const auto cb3 = [&cb](const auto& a, auto b) { cb.template operator()<3>(a, b); };
        const auto cb4 = [&cb](const auto& a, auto b) { cb.template operator()<4>(a, b); };

        if(comp == 0) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fastasset, fastacc, cb3); }
        else if(comp == 1) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fastasset, fastacc, cb3); }
        else if(comp == 2) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(fastasset, fastacc, cb4); }
        else if(comp == 3) { fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(fastasset, fastacc, cb2); }
    };

    for(auto i = 0u; i < std::size(FAST_COMPS); ++i)
    {
        if(!vertex_layout.test(GFX_COMPS[i]))
        {
            if(i == 0)
            {
                ENG_ERROR("Mesh does not have positions");
                return {};
            }
            continue;
        }

        auto it = fprim.findAttribute(FAST_COMPS[i]);
        auto& acc = fastasset.accessors.at(it->accessorIndex);
        if(i == 0) { vertices.resize(acc.count * vertex_size / sizeof(float)); }
        fast_iterate(i, acc, GFX_COMPS[i]);
    }

    if(!fprim.indicesAccessor)
    {
        ENG_WARN("Mesh ({}) primitive ({}) does not have mandatory vertex indices. Skipping...", fastmesh.name.c_str(), primitive_index);
        return nullptr;
    }
    else
    {
        auto& acc = fastasset.accessors.at(*fprim.indicesAccessor);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return nullptr;
        }
        indices.resize(acc.count);
        fastgltf::copyFromAccessor<uint32_t>(fastasset, acc, indices.data());
    }

    const auto render_geometry = Engine::get().renderer->make_geometry(gfx::GeometryDescriptor{
        .flags = {},
        .vertex_layout = vertex_layout,
        .vertices = vertices,
        .indices = std::span{ indices },
    });

    auto& geom = model.geometries.emplace_back();
    geom.render_geometry = render_geometry;
    geom.bvh = physics::BVH{ std::as_bytes(std::span{ vertices }), gfx::get_vertex_layout_size(vertex_layout),
                             std::as_bytes(std::span{ indices }), gfx::IndexFormat::U32 };

    return &geom;
}

asset::Image* load_image(const fastgltf::Asset& fastasset, gfx::ImageFormat format, size_t image_index, asset::Model& model)
{
    // todo: check if image format matches with the currently requested.
    if(model.images.size() <= image_index) { model.images.resize(fastasset.images.size()); }
    if(model.images.at(image_index).render_image) { return &model.images.at(image_index); }

    const auto& fimg = fastasset.images.at(image_index);
    std::span<const std::byte> data;
    if(auto* fastbufviewsrc = std::get_if<fastgltf::sources::BufferView>(&fimg.data))
    {
        auto& fastbufview = fastasset.bufferViews.at(fastbufviewsrc->bufferViewIndex);
        auto& fastbuf = fastasset.buffers.at(fastbufview.bufferIndex);
        if(auto* fastarrsrc = std::get_if<fastgltf::sources::Array>(&fastbuf.data))
        {
            data = { fastarrsrc->bytes.data() + fastbufview.byteOffset, fastbufview.byteLength };
        }
    }
    if(data.empty())
    {
        ENG_WARN("Could not load image {}", fimg.name.c_str());
        return nullptr;
    }

    int x, y, ch;
    std::byte* imgdata = reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                                            data.size(), &x, &y, &ch, 4));
    if(!imgdata)
    {
        ENG_ERROR("Stbi failed for image {}: {}", fimg.name.c_str(), stbi_failure_reason());
        return nullptr;
    }

    const auto img =
        Engine::get().renderer->make_image(gfx::Image::init(fimg.name.c_str(), (uint32_t)x, (uint32_t)y, 0, format,
                                                            gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                            0, 1, gfx::ImageLayout::READ_ONLY));
    ENG_TODO("TODO: Process mips");
    gfx::get_renderer().staging->copy(img, imgdata, 0, 0, true, gfx::DiscardContents::YES);
    stbi_image_free(imgdata);
    model.images.at(image_index).name = fimg.name.c_str();
    model.images.at(image_index).render_image = img;
    return &model.images.at(image_index);
}

// static Handle<gfx::Sampler> load_sampler(const fastgltf::Asset& asset, size_t index, eng::LoadedModel& ctx)
//{
//     if(index == ~0ull) { return Engine::get().renderer->make_sampler(gfx::SamplerDescriptor{}); }
//     if(ctx.samplers.size() <= index) { ctx.samplers.resize(asset.samplers.size()); }
//     if(ctx.samplers.at(index)) { return ctx.samplers.at(index); }
//
//     const auto& fsamp = asset.samplers.at(index);
//     auto sampd = gfx::SamplerDescriptor{};
//     if(fsamp.minFilter)
//     {
//         if(*fsamp.minFilter == fastgltf::Filter::Nearest) { sampd.filtering[0] = gfx::ImageFilter::NEAREST; }
//         else if(*fsamp.minFilter == fastgltf::Filter::Linear) { sampd.filtering[0] = gfx::ImageFilter::LINEAR; }
//         else if(*fsamp.minFilter == fastgltf::Filter::LinearMipMapNearest)
//         {
//             sampd.filtering[0] = gfx::ImageFilter::NEAREST;
//             sampd.mipmap_mode = gfx::SamplerMipmapMode::NEAREST;
//         }
//         else if(*fsamp.minFilter == fastgltf::Filter::LinearMipMapLinear)
//         {
//             sampd.filtering[0] = gfx::ImageFilter::LINEAR;
//             sampd.mipmap_mode = gfx::SamplerMipmapMode::LINEAR;
//         }
//     }
//     if(fsamp.magFilter)
//     {
//         if(*fsamp.magFilter == fastgltf::Filter::Nearest) { sampd.filtering[1] = gfx::ImageFilter::NEAREST; }
//         else if(*fsamp.magFilter == fastgltf::Filter::Linear) { sampd.filtering[1] = gfx::ImageFilter::LINEAR; }
//     }
//     const auto sampler = Engine::get().renderer->make_sampler(sampd);
//     ctx.samplers.at(index) = sampler;
//     return sampler;
// }

asset::Texture* load_texture(const fastgltf::Asset& fastasset, gfx::ImageFormat format, size_t texture_index, asset::Model& model)
{
    if(model.textures.size() <= texture_index) { model.textures.resize(fastasset.textures.size()); }
    if(model.textures.at(texture_index).view) { return &model.textures.at(texture_index); }
    const auto& ftex = fastasset.textures.at(texture_index);
    const auto image_index = ftex.imageIndex ? *ftex.imageIndex : ~0ull;
    if(image_index == ~0ull) { return nullptr; }
    auto* image = load_image(fastasset, format, image_index, model);
    if(!image)
    {
        ENG_ERROR("Could not load texture ({}) image ({})", ftex.name.c_str(), texture_index);
        return nullptr;
    }
    model.textures.at(texture_index) = Texture{ ftex.name.c_str(), gfx::ImageView::init(image->render_image) };
    return &model.textures.at(texture_index);
}

asset::Material* load_material(const fastgltf::Asset& fastasset, const fastgltf::Mesh& fastmesh,
                               uint32_t primitive_index, asset::Model& model)
{
    const auto& fprim = fastmesh.primitives.at(primitive_index);
    if(!fprim.materialIndex) { return nullptr; }
    const auto& fmat = fastasset.materials.at(*fprim.materialIndex);
    if(model.materials.size() <= *fprim.materialIndex) { model.materials.resize(fastasset.materials.size()); }
    if(model.materials.at(*fprim.materialIndex).render_material) { return &model.materials.at(*fprim.materialIndex); }

    auto matdesc = gfx::MaterialDescriptor{};
    if(fmat.pbrData.baseColorTexture)
    {
        auto* tex = gltf::load_texture(fastasset, gfx::ImageFormat::R8G8B8A8_SRGB, fmat.pbrData.baseColorTexture->textureIndex, model);
        if(tex != nullptr) { matdesc.base_color_texture = tex->view; }
        else { ENG_ERROR("Could not load base color texture for material ({}).", fmat.name.c_str()); }
    }
    if(fmat.normalTexture)
    {
        auto* tex = gltf::load_texture(fastasset, gfx::ImageFormat::R8G8B8A8_UNORM, fmat.normalTexture->textureIndex, model);
        if(tex != nullptr) { matdesc.normal_texture = tex->view; }
        else { ENG_ERROR("Could not load normal texture for material ({}).", fmat.name.c_str()); }
    }
    if(fmat.pbrData.metallicRoughnessTexture)
    {
        auto* tex = gltf::load_texture(fastasset, gfx::ImageFormat::R8G8B8A8_UNORM,
                                       fmat.pbrData.metallicRoughnessTexture->textureIndex, model);
        if(tex != nullptr) { matdesc.metallic_roughness_texture = tex->view; }
        else { ENG_ERROR("Could not load metallic roughness texture for material ({}).", fmat.name.c_str()); }
    }
    const auto mat = Engine::get().renderer->make_material(matdesc);
    model.materials.at(*fprim.materialIndex).name = fmat.name.c_str();
    model.materials.at(*fprim.materialIndex).render_material = mat;
    return &model.materials.at(*fprim.materialIndex);
}

asset::Mesh* load_mesh(const fastgltf::Asset& fastasset, const fastgltf::Node& fastnode, asset::Model& model)
{
    if(!fastnode.meshIndex) { return nullptr; }
    if(model.meshes.size() <= *fastnode.meshIndex) { model.meshes.resize(fastasset.meshes.size()); }
    if(model.meshes.at(*fastnode.meshIndex).render_meshes.size() > 0) { return &model.meshes.at(*fastnode.meshIndex); }

    const auto& fm = fastasset.meshes.at(*fastnode.meshIndex);
    std::vector<Handle<gfx::Mesh>> gfxmeshes;
    std::vector<uint32_t> geometries;
    for(auto i = 0u; i < fm.primitives.size(); ++i)
    {
        const auto* geom = gltf::load_geometry(fastasset, fm, i, model);
        const auto* mat = gltf::load_material(fastasset, fm, i, model);
        if(!geom)
        {
            ENG_ERROR("Failed to load geometry for mesh ({}) primitive ({}).", fm.name.c_str(), i);
            continue;
        }
        if(!mat)
        {
            ENG_ERROR("Failed to load material for mesh ({}) primitive ({}).", fm.name.c_str(), i);
            continue;
        }
        gfxmeshes.push_back(Engine::get().renderer->make_mesh(gfx::MeshDescriptor{ .geometry = geom->render_geometry,
                                                                                   .material = mat->render_material }));
    }
    auto& mesh = model.meshes.at(*fastnode.meshIndex);
    mesh.name = fm.name.c_str();
    mesh.render_meshes = std::move(gfxmeshes);
    mesh.geometries = { (uint32_t)(model.geometries.size() - fm.primitives.size()), (uint32_t)fm.primitives.size() };
    return &mesh;
}

void load_node(const fastgltf::Asset& fastasset, const fastgltf::Node& fastnode, asset::Model& model, asset::Model::Node& node)
{
    node.name = fastnode.name.c_str();

    if(fastnode.transform.index() == 0)
    {
        const auto& trs = std::get<fastgltf::TRS>(fastnode.transform);
        node.transform =
            (glm::translate(glm::mat4{ 1.0f }, glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() }) *
             glm::mat4_cast(glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() }) *
             glm::scale(glm::mat4{ 1.0f }, glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() })) *
            node.transform;
    }
    else
    {
        const auto& trs = std::get<fastgltf::math::fmat4x4>(fastnode.transform);
        glm::mat4 glmtrs;
        memcpy(&glmtrs, &trs, sizeof(trs));
        node.transform = glmtrs * node.transform;
    }

    if(fastnode.meshIndex)
    {
        auto* mesh = gltf::load_mesh(fastasset, fastnode, model);
        if(!mesh)
        {
            ENG_ERROR("Failed to load mesh ({}) for node ({}).", fastasset.meshes.at(*fastnode.meshIndex).name.c_str(),
                      fastnode.name.c_str());
        }
        else { node.mesh = *fastnode.meshIndex; }
    }

    node.children.reserve(fastnode.children.size());
    for(const auto& e : fastnode.children)
    {
        node.children.push_back(model.nodes.size());
        auto& child = model.nodes.emplace_back();
        child.transform = node.transform;
        gltf::load_node(fastasset, fastasset.nodes.at(e), model, child);
    }
}
} // namespace gltf

std::expected<asset::Model, std::string> GLTFModelImporter::load_model(const std::filesystem::path& path)
{
    if(!std::filesystem::exists(path))
    {
        return std::unexpected(ENG_FMT("Path {} does not point to any file.", path.string()));
    }

    if(path.extension() != ".glb") { return std::unexpected("Only glb files are supported."); }

    auto fastdatabuf = fastgltf::GltfDataBuffer::FromPath(path);
    if(!fastdatabuf)
    {
        return std::unexpected(ENG_FMT("Error during GLTF import: {}", fastgltf::getErrorName(fastdatabuf.error())));
    }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    fastgltf::Parser fastparser;
    auto fastexpasset = fastparser.loadGltfBinary(fastdatabuf.get(), path.parent_path(), gltfOptions);
    if(!fastexpasset)
    {
        return std::unexpected(ENG_FMT("Error during loading fastgltf::Parser::loadGltfBinary: {}",
                                       fastgltf::getErrorName(fastexpasset.error())));
    }

    auto& fastasset = fastexpasset.get();
    if(fastasset.scenes.empty())
    {
        return std::unexpected("Error during loading. Fastgltf asset does not have any scenes defined.");
    }

    auto& fastscene = fastasset.scenes.at(0);
    auto& r = gfx::get_renderer();
    asset::Model model;
    model.nodes.reserve(fastasset.nodes.size() + 1);
    auto root_node = asset::Model::Node{};
    root_node.name = path.filename().replace_extension("").string();
    root_node.children.resize(fastscene.nodeIndices.size());
    for(auto i = 0u; i < fastscene.nodeIndices.size(); ++i)
    {
        root_node.children.at(i) = model.nodes.size();
        auto& child = model.nodes.emplace_back();
        gltf::load_node(fastasset, fastasset.nodes.at(fastscene.nodeIndices.at(i)), model, child);
    }
    model.root_node = model.nodes.size();
    model.nodes.push_back(std::move(root_node));
    ENG_ASSERT(model.nodes.capacity() == fastasset.nodes.size() + 1);
    return std::move(model);
}
} // namespace import
} // namespace asset

void Scene::init() { asset::import::file_importers[".glb"] = std::make_unique<asset::import::GLTFModelImporter>(); }

asset::Model* Scene::load_from_file(const std::filesystem::path& _path)
{
    const auto filepath = eng::paths::MODELS_DIR / _path;
    const auto fileext = filepath.extension();

    if(const auto it = loaded_models.find(filepath); it != loaded_models.end()) { return &it->second; }

    if(const auto it = asset::import::file_importers.find(fileext); it != asset::import::file_importers.end())
    {
        auto model = it->second->load_model(filepath);
        if(!model)
        {
            ENG_WARN("{}", model.error());
            return nullptr;
        }
        return &loaded_models.emplace(filepath, std::move(*model)).first->second;
    }

    ENG_WARN("No importer for extensions {}.", fileext.string());
    return nullptr;
}

ecs::entity Scene::instance_model(const asset::Model* model)
{
    if(!model) { return eng::ecs::INVALID_ENTITY; }

    static constexpr auto make_hierarchy = [](const auto& self, const asset::Model& model,
                                              const asset::Model::Node& node, ecs::entity parent) -> ecs::entity {
        auto* ecsr = Engine::get().ecs;
        auto entity = ecsr->create();
        ecsr->emplace(entity, ecs::Node{ node.name, &model });
        ecsr->emplace(entity, ecs::Transform{ glm::mat4{ 1.0f }, node.transform });
        if(node.mesh != ~0u) { ecsr->emplace(entity, ecs::Mesh{ &model.meshes.at(node.mesh), ~0u }); }
        if(parent != ecs::INVALID_ENTITY) { ecsr->make_child(parent, entity); }
        for(const auto& e : node.children)
        {
            self(self, model, model.nodes.at(e), entity);
        }
        return entity;
    };

    const auto instance = make_hierarchy(make_hierarchy, *model, model->nodes.at(model->root_node), ecs::INVALID_ENTITY);
    scene.push_back(instance);
    return instance;
}

void Scene::update_transform(ecs::entity entity)
{
    if(entity == ecs::INVALID_ENTITY) { return; }
    auto* et = Engine::get().ecs->get<ecs::Transform>(entity);
    if(!et)
    {
        ENG_WARN("Entity does not have transform component.");
        return;
    }
    pending_transforms.push_back(entity);
}

void Scene::update()
{
    // Relies on pending transforms not having child nodes of other nodes (no two nodes from the same hierarchy)
    if(pending_transforms.size())
    {
        std::unordered_set<ecs::entity> visited;

        // leave only those entities, who have no ancestors in the pending trs.
        std::vector<ecs::entity> filtered;
        filtered.reserve(pending_transforms.size());
        visited.insert(pending_transforms.begin(), pending_transforms.end());
        for(auto e : pending_transforms)
        {
            auto p = e;
            auto passes = true;
            while(p != ecs::INVALID_ENTITY)
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
            std::stack<ecs::entity> visit;
            std::stack<glm::mat4> trs;
            visit.push(e);
            if(p != ecs::INVALID_ENTITY)
            {
                auto* pt = Engine::get().ecs->get<ecs::Transform>(p);
                trs.push(pt->global);
            }
            else { trs.push(glm::identity<glm::mat4>()); }

            while(visit.size())
            {
                ENG_ASSERT(trs.size() == visit.size());
                auto e = visit.top();
                auto pt = trs.top();
                visit.pop();
                trs.pop();
                auto* t = Engine::get().ecs->get<ecs::Transform>(e);
                t->global = t->local * pt;
                Engine::get().renderer->update_transform(e);
                const auto& ech = Engine::get().ecs->get_children(e);
                for(auto i = 0u; i < ech.size(); ++i)
                {
                    trs.push(t->global);
                    visit.push(ech[i]);
                }
            }
        }
        pending_transforms.clear();
    }
}

void Scene::ui_draw_scene()
{
    const auto expand_hierarchy = [this](ecs::Registry* reg, ecs::entity e, bool expand, const auto& self) -> void {
        ui.scene.nodes[e].expanded = expand;
        for(auto ch : reg->get_children(e))
        {
            self(reg, ch, expand, self);
        }
    };

    const auto draw_hierarchy = [&, this](ecs::Registry* reg, ecs::entity e, const auto& self) -> void {
        const auto enode = reg->get<ecs::Node>(e);
        const auto& echildren = reg->get_children(e);
        ImGui::PushID((int)e);
        auto& ui_node = ui.scene.nodes[e];
        // ImGui::BeginGroup();
        if(echildren.size())
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
            if(ImGui::Selectable(enode->name.c_str(), &is_sel)) { ui.scene.sel_entity = e; }
        }
        // ImGui::EndGroup();
        if(ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(0))
        {
            expand_hierarchy(reg, e, !ui_node.expanded, expand_hierarchy);
        }

        if(ui_node.expanded)
        {
            ImGui::Indent();
            for(const auto& ec : echildren)
            {
                self(reg, ec, self);
            }
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
    if(ui.scene.sel_entity == ecs::INVALID_ENTITY) { return; }

    auto* ecs = Engine::get().ecs;
    auto& entity = ui.scene.sel_entity;
    auto& uie = ui.scene.nodes.at(entity);
    auto* ctransform = ecs->get<ecs::Transform>(entity);
    auto* cnode = ecs->get<ecs::Node>(entity);
    auto* cmesh = ecs->get<ecs::Mesh>(entity);
    auto* clight = ecs->get<ecs::Light>(entity);

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

void Scene::ui_draw_manipulate()
{
    if(ui.scene.sel_entity == ecs::INVALID_ENTITY) { return; }

    auto* ecs = Engine::get().ecs;
    auto& entity = ui.scene.sel_entity;
    auto* ctransform = ecs->get<ecs::Transform>(entity);
    auto* cnode = ecs->get<ecs::Node>(entity);
    auto* cmesh = ecs->get<ecs::Mesh>(entity);

    auto& io = ImGui::GetIO();
    auto& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0u); // don't set no background, make host dock push style with no bg, and somehow it works -
                                                  // the content window actually does not have the background
    ImGui::Begin("Manipulate", 0, ImGuiWindowFlags_NoDecoration);
    ImGui::PopStyleColor(1);
    ImGuizmo::SetDrawlist();

    const auto view = Engine::get().camera->get_view();
    auto proj = Engine::get().camera->get_projection(); // imguizmo hates inf_revz_zo perspective matrix that i use (div by 0 because no far plane)
    proj = glm::perspectiveFov(glm::radians(75.0f), Engine::get().window->width, Engine::get().window->height, 0.1f, 30.0f);
    const auto window_width = ImGui::GetWindowWidth();
    const auto window_height = ImGui::GetWindowHeight();
    const auto window_pos = ImGui::GetWindowPos();
    glm::mat4 tr{ 1.0f };
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    auto translation = ctransform->global;
    glm::mat4 delta;
    if(ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::LOCAL,
                            &ctransform->local[0][0]))
    {
        update_transform(entity);
    }

    ImGui::End();
}
} // namespace eng
