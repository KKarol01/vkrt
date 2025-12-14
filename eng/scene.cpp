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
#include <eng/ui.hpp>
#include <eng/ecs/components.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/paths.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/camera.hpp>
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
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    if(auto it = fprim.findAttribute("POSITION"); it != fprim.attributes.end())
    {
        auto& acc = fastasset.accessors.at(it->accessorIndex);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return nullptr;
        }
        vertices.resize(acc.count);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(fastasset, acc, [&vertices](const auto& vec, auto idx) {
            vertices.at(idx).position = { vec.x(), vec.y(), vec.z() };
        });
    }
    else
    {
        ENG_WARN("Mesh primitive does not contain position. Skipping...");
        return nullptr;
    }
    if(auto it = fprim.findAttribute("NORMAL"); it != fprim.attributes.end())
    {
        auto& acc = fastasset.accessors.at(it->accessorIndex);
        if(!acc.bufferViewIndex)
        {
            ENG_ERROR("No bufferViewIndex...");
            return nullptr;
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
            return nullptr;
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
            return nullptr;
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(fastasset, acc, [&vertices](const auto& vec, auto idx) {
            vertices.at(idx).tangent = { vec.x(), vec.y(), vec.z(), vec.w() };
        });
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

    const auto render_geometry =
        Engine::get().renderer->make_geometry(gfx::GeometryDescriptor{ .vertices = vertices, .indices = indices });

    auto& geom = model.geometries.emplace_back();
    geom.render_geometry = render_geometry;
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

    auto imgd = gfx::ImageDescriptor{};
    imgd.name = fimg.name.c_str();
    int x, y, ch;
    std::byte* imgdata = reinterpret_cast<std::byte*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                                            data.size(), &x, &y, &ch, 4));
    if(!imgdata)
    {
        ENG_ERROR("Stbi failed for image {}: {}", fimg.name.c_str(), stbi_failure_reason());
        return nullptr;
    }

    imgd.usage = gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT | gfx::ImageUsage::TRANSFER_SRC_BIT;
    imgd.data = { imgdata, imgdata + x * y * ch };
    imgd.width = (uint32_t)x;
    imgd.height = (uint32_t)y;
    imgd.format = format;
    imgd.mips = std::log2(std::min(x, y)) + 1;
    const auto img = Engine::get().renderer->make_image(imgd);
    stbi_image_free(imgdata);
    model.images.at(image_index).name = imgd.name;
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
    if(model.textures.at(texture_index).render_texture) { return &model.textures.at(texture_index); }
    const auto& ftex = fastasset.textures.at(texture_index);
    const auto image_index = ftex.imageIndex ? *ftex.imageIndex : ~0ull;
    if(image_index == ~0ull) { return nullptr; }
    auto* image = load_image(fastasset, format, image_index, model);
    if(image == nullptr)
    {
        ENG_ERROR("Could not load texture ({}) image ({})", ftex.name.c_str(), texture_index);
        return nullptr;
    }
    const auto tex = Engine::get().renderer->make_texture(gfx::TextureDescriptor{
        .view = image->render_image->default_view,
        .layout = gfx::ImageLayout::READ_ONLY,
        //.sampler = load_sampler(asset, ftex.samplerIndex ? *ftex.samplerIndex : ~0ull, model), // todo: somehow pass the sampler -- or set it in the gui, once it's done...
        .is_storage = false });

    model.textures.at(texture_index).name = ftex.name.c_str();
    model.textures.at(texture_index).render_texture = tex;
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
        if(tex != nullptr) { matdesc.base_color_texture = tex->render_texture; }
        else { ENG_ERROR("Could not load base color texture for material ({}).", fmat.name.c_str()); }
    }
    if(fmat.normalTexture)
    {
        auto* tex = gltf::load_texture(fastasset, gfx::ImageFormat::R8G8B8A8_UNORM, fmat.normalTexture->textureIndex, model);
        if(tex != nullptr) { matdesc.normal_texture = tex->render_texture; }
        else { ENG_ERROR("Could not load normal texture for material ({}).", fmat.name.c_str()); }
    }
    if(fmat.pbrData.metallicRoughnessTexture)
    {
        auto* tex = gltf::load_texture(fastasset, gfx::ImageFormat::R8G8B8A8_UNORM,
                                       fmat.pbrData.metallicRoughnessTexture->textureIndex, model);
        if(tex != nullptr) { matdesc.metallic_roughness_texture = tex->render_texture; }
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
    gfxmeshes.reserve(fm.primitives.size());
    for(auto i = 0u; i < fm.primitives.size(); ++i)
    {
        const auto* geom = gltf::load_geometry(fastasset, fm, i, model);
        const auto* mat = gltf::load_material(fastasset, fm, i, model);
        if(geom == nullptr)
        {
            ENG_ERROR("Failed to load geometry for mesh ({}) primitive ({}).", fm.name.c_str(), i);
            continue;
        }
        if(mat == nullptr)
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
        if(mesh == nullptr)
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

Model GLTFModelImporter::load_model(const std::filesystem::path& path)
{
    const auto filepath = eng::paths::canonize_path(eng::paths::MODELS_DIR / path);

    if(!std::filesystem::exists(filepath))
    {
        ENG_WARN("Path {} does not point to any file.", filepath.string());
        return {};
    }

    if(filepath.extension() != ".glb")
    {
        ENG_WARN("Only glb files are supported.");
        return {};
    }

    auto fastdatabuf = fastgltf::GltfDataBuffer::FromPath(filepath);
    if(!fastdatabuf)
    {
        ENG_WARN("Error during GLTF import: {}", fastgltf::getErrorName(fastdatabuf.error()));
        return {};
    }

    static constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers |
                                        fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
    fastgltf::Parser fastparser;
    auto fastexpasset = fastparser.loadGltfBinary(fastdatabuf.get(), filepath.parent_path(), gltfOptions);
    if(!fastexpasset)
    {
        ENG_WARN("Error during loading fastgltf::Parser::loadGltfBinary: {}", fastgltf::getErrorName(fastexpasset.error()));
        return {};
    }

    auto& fastasset = fastexpasset.get();
    if(fastasset.scenes.empty())
    {
        ENG_WARN("Error during loading. Fastgltf asset does not have any scenes defined.");
        return {};
    }

    auto& fastscene = fastasset.scenes.at(0);
    auto* r = Engine::get().renderer;
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
    assert(model.nodes.capacity() == fastasset.nodes.size() + 1);
    return model;
}
} // namespace import
} // namespace asset

void Scene::init()
{
    asset::import::file_importers[".glb"] = std::make_unique<asset::import::GLTFModelImporter>();

    Engine::get().ui->add_tab(UI::Tab{
        .name = "Scene", .location = UI::Location::LEFT_PANE, .cb_func = [this] { ui_draw_scene(); } });
    Engine::get().ui->add_tab(UI::Tab{
        .name = "Inspector", .location = UI::Location::RIGHT_PANE, .cb_func = [this] { ui_draw_inspector(); } });
    Engine::get().ui->add_tab(UI::Tab{
        .name = "Manipulate", .location = UI::Location::CENTER_PANE, .cb_func = [this] { ui_draw_manipulate(); } });
}

asset::Model* Scene::load_from_file(const std::filesystem::path& _path)
{
    const auto filepath = eng::paths::canonize_path(eng::paths::MODELS_DIR / _path);
    const auto fileext = filepath.extension();

    if(const auto it = loaded_models.find(filepath); it != loaded_models.end()) { return &it->second; }

    if(const auto it = asset::import::file_importers.find(fileext); it != asset::import::file_importers.end())
    {
        return &loaded_models.emplace(filepath, it->second->load_model(filepath)).first->second;
    }

    ENG_WARN("No importer for extensions {}.", fileext.string());
    return nullptr;
}

ecs::entity Scene::instance_model(const asset::Model* model)
{
    if(model == nullptr) { return eng::ecs::INVALID_ENTITY; }

    static constexpr auto make_hierarchy = [](const auto& self, const asset::Model& model,
                                              const asset::Model::Node& node, ecs::entity parent) -> ecs::entity {
        auto* ecsr = Engine::get().ecs;
        auto entity = ecsr->create();
        ecsr->emplace(entity, ecs::Node{ node.name });
        ecsr->emplace(entity, ecs::Transform{ glm::mat4{ 1.0f }, node.transform });
        if(node.mesh != ~0u)
        {
            ecsr->emplace(entity, ecs::Mesh{ &model.meshes.at(node.mesh), model.meshes.at(node.mesh).render_meshes, ~0u });
        }
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
                assert(trs.size() == visit.size());
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
    if(ImGui::Begin("Scene", 0, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const auto draw_hierarchy = [this](ecs::Registry* reg, ecs::entity e, const auto& self) -> void {
            const auto enode = reg->get<ecs::Node>(e);
            const auto& echildren = reg->get_children(e);
            auto idd = ImGui::GetID(enode->name.c_str());
            ImGui::PushID(enode->name.c_str());
            ImGui::PushID((int)e);
            const auto imhid = ImGui::GetItemID();
            assert(idd != imhid);
            auto& ui_node = ui.scene.nodes[imhid];
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
            ImGui::PopID();
        };
        for(const auto& e : scene)
        {
            draw_hierarchy(Engine::get().ecs, e, draw_hierarchy);
        }
    }
    ImGui::End();
}

void Scene::ui_draw_inspector()
{
    if(ui.scene.sel_entity == ecs::INVALID_ENTITY) { return; }

    auto* ecs = Engine::get().ecs;
    auto& entity = ui.scene.sel_entity;
    auto* ctransform = ecs->get<ecs::Transform>(entity);
    auto* cnode = ecs->get<ecs::Node>(entity);
    auto* cmesh = ecs->get<ecs::Mesh>(entity);
    auto* clight = ecs->get<ecs::Light>(entity);

    if(ImGui::Begin("Inspector"))
    {
        assert(cnode && ctransform);
        ImGui::SeparatorText("Node");
        ImGui::SeparatorText("Transform");
        if(ImGui::DragFloat3("Position", &ctransform->local[3].x)) { update_transform(entity); }
        if(cmesh)
        {
            ImGui::SeparatorText("Mesh renderer");
            ImGui::Text(cmesh->mesh->name.c_str());
            if(cmesh->meshes.size())
            {
                // ImGui::Indent();
                for(auto& e : cmesh->meshes)
                {
                    auto& material = e->material.get();
                    ImGui::Text("Pass: %s", material.mesh_pass->name.c_str());
                    if(material.base_color_texture)
                    {
                        ImGui::Image(*material.base_color_texture + 1, { 128.0f, 128.0f });
                    }
                }
                // ImGui::Unindent();
            }
        }
        if(clight)
        {
            ImGui::SeparatorText("Light");
            ImGui::Text("Type: %s", to_string(clight->type).c_str());
            bool edited = false;
            edited |= ImGui::ColorEdit4("Color", &clight->color.x);
            edited |= ImGui::SliderFloat("Range", &clight->range, 0.0f, 100.0f);
            edited |= ImGui::SliderFloat("Intensity", &clight->intensity, 0.0f, 100.0f);
            // todo: don't like that entities with light component have to be detected and handled separately
            if(edited) { update_transform(entity); }
        }
    }
    ImGui::End();
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
