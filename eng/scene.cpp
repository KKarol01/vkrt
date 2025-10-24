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
static Handle<gfx::Geometry> load_geometry(const fastgltf::Asset& asset, const fastgltf::Mesh& mesh,
                                           uint32_t primitive_index, eng::LoadedNode& ctx)
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

    const auto geom = Engine::get().renderer->make_geometry(gfx::GeometryDescriptor{ .vertices = vertices, .indices = indices });
    ctx.geometries.push_back(geom);
    return geom;
}

static Handle<gfx::Image> load_image(const fastgltf::Asset& asset, gfx::ImageFormat format, size_t index, eng::LoadedNode& ctx)
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

    imgd.usage = gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT | gfx::ImageUsage::TRANSFER_SRC_BIT;
    imgd.data = { imgdata, imgdata + x * y * ch };
    imgd.width = (uint32_t)x;
    imgd.height = (uint32_t)y;
    imgd.format = format;
    imgd.mips = std::log2(std::min(x, y)) + 1;
    const auto img = Engine::get().renderer->make_image(imgd);
    stbi_image_free(imgdata);
    ctx.images.at(index) = img;
    return img;
}

static Handle<gfx::Sampler> load_sampler(const fastgltf::Asset& asset, size_t index, eng::LoadedNode& ctx)
{
    if(index == ~0ull) { return Engine::get().renderer->make_sampler(gfx::SamplerDescriptor{}); }
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
            sampd.mipmap_mode = gfx::SamplerMipmapMode::NEAREST;
        }
        else if(*fsamp.minFilter == fastgltf::Filter::LinearMipMapLinear)
        {
            sampd.filtering[0] = gfx::ImageFilter::LINEAR;
            sampd.mipmap_mode = gfx::SamplerMipmapMode::LINEAR;
        }
    }
    if(fsamp.magFilter)
    {
        if(*fsamp.magFilter == fastgltf::Filter::Nearest) { sampd.filtering[1] = gfx::ImageFilter::NEAREST; }
        else if(*fsamp.magFilter == fastgltf::Filter::Linear) { sampd.filtering[1] = gfx::ImageFilter::LINEAR; }
    }
    const auto sampler = Engine::get().renderer->make_sampler(sampd);
    ctx.samplers.at(index) = sampler;
    return sampler;
}

static Handle<gfx::Texture> load_texture(const fastgltf::Asset& asset, gfx::ImageFormat format, size_t index, eng::LoadedNode& ctx)
{
    if(index == ~0ull) { return {}; }
    if(ctx.textures.size() <= index) { ctx.textures.resize(asset.textures.size()); }
    if(ctx.textures.at(index)) { return ctx.textures.at(index); }

    const auto& ftex = asset.textures.at(index);
    const auto tex = Engine::get().renderer->make_texture(gfx::TextureDescriptor{
        .view = load_image(asset, format, ftex.imageIndex ? *ftex.imageIndex : ~0ull, ctx)->default_view,
        .layout = gfx::ImageLayout::READ_ONLY,
        //.sampler = load_sampler(asset, ftex.samplerIndex ? *ftex.samplerIndex : ~0ull, ctx), // todo: somehow pass the sampler -- or set it in the gui, once it's done...
        .is_storage = false });
    ctx.textures.at(index) = tex;
    return tex;
}

static Handle<gfx::Material> load_material(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, eng::LoadedNode& ctx)
{
    if(!primitive.materialIndex) { return {}; }
    if(ctx.materials.size() <= *primitive.materialIndex) { ctx.materials.resize(asset.materials.size()); }
    if(ctx.materials.at(*primitive.materialIndex)) { return ctx.materials.at(*primitive.materialIndex); }

    const auto& fmat = asset.materials.at(*primitive.materialIndex);
    const auto mat = Engine::get().renderer->make_material(gfx::MaterialDescriptor{
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

static void load_mesh(ecs::entity e, const fastgltf::Asset& asset, const fastgltf::Node& node, eng::LoadedNode& ctx)
{
    auto* ecsr = Engine::get().ecs;
    if(!node.meshIndex) { return; }

    const auto& fm = asset.meshes.at(*node.meshIndex);
    if(ctx.meshes.size() <= *node.meshIndex) { ctx.meshes.resize(asset.meshes.size()); }
    if(ctx.meshes.at(*node.meshIndex).meshes.size())
    {
        ecsr->emplace<ecs::Mesh>(e, ctx.meshes.at(*node.meshIndex));
        return;
    }

    auto& m = ctx.meshes.at(*node.meshIndex);
    m.name = fm.name.c_str();
    m.meshes.resize(fm.primitives.size());
    for(auto i = 0u; i < fm.primitives.size(); ++i)
    {
        const auto geom = load_geometry(asset, fm, i, ctx);
        const auto mat = load_material(asset, fm.primitives.at(i), ctx);
        m.meshes.at(i) = Engine::get().renderer->make_mesh(gfx::MeshDescriptor{ .geometry = geom, .material = mat });
    }
    ecsr->emplace<ecs::Mesh>(e, m);
}

static ecs::entity load_node(const fastgltf::Scene& scene, const fastgltf::Asset& asset, const fastgltf::Node& node,
                             eng::LoadedNode& ctx, glm::mat4 transform = { 1.0f })
{
    auto* ecsr = Engine::get().ecs;
    auto entity = ecsr->create();

    ecsr->emplace<ecs::Node>(entity, ecs::Node{ node.name.c_str() });

    glm::mat4 glm_global = transform;
    glm::mat4 glm_local;
    if(node.transform.index() == 0)
    {
        const auto& trs = std::get<fastgltf::TRS>(node.transform);
        glm_local =
            glm::translate(glm::mat4{ 1.0f }, glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() }) *
            glm::mat4_cast(glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() }) *
            glm::scale(glm::mat4{ 1.0f }, glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
        glm_global = glm_local * glm_global;
    }
    else
    {
        const auto& trs = std::get<fastgltf::math::fmat4x4>(node.transform);
        memcpy(&glm_local, &trs, sizeof(trs));
        glm_global = glm_local * glm_global;
    }
    ecsr->emplace<ecs::Transform>(entity, ecs::Transform{ .local = glm_local, .global = glm_global });

    load_mesh(entity, asset, node, ctx);

    for(auto e : node.children)
    {
        ecsr->make_child(entity, load_node(scene, asset, asset.nodes.at(e), ctx, glm_global));
    }

    return entity;
}

void Scene::init()
{
    Engine::get().ui->add_tab(UI::Tab{
        .name = "Scene", .location = UI::Location::LEFT_PANE, .cb_func = [this] { ui_draw_scene(); } });
    Engine::get().ui->add_tab(UI::Tab{
        .name = "Inspector", .location = UI::Location::RIGHT_PANE, .cb_func = [this] { ui_draw_inspector(); } });
    Engine::get().ui->add_tab(UI::Tab{
        .name = "Manipulate", .location = UI::Location::CENTER_PANE, .cb_func = [this] { ui_draw_manipulate(); } });
}

ecs::entity Scene::load_from_file(const std::filesystem::path& _path)
{
    const auto filepath = eng::paths::canonize_path(eng::paths::MODELS_DIR / _path);

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
    eng::LoadedNode ctx;

    auto root = ecs::INVALID_ENTITY;
    if(fscene.nodeIndices.size() == 1)
    {
        root = load_node(fscene, fasset, fasset.nodes.at(fscene.nodeIndices.front()), ctx);
    }
    else if(fscene.nodeIndices.size() > 1)
    {
        root = ecsr->create();
        ecsr->emplace<ecs::Node>(root, ecs::Node{ .name = filepath.stem().string() });
        ecsr->emplace<ecs::Transform>(root, ecs::Transform{ .local = glm::identity<glm::mat4>(),
                                                            .global = glm::identity<glm::mat4>() });
        for(const auto& fsni : fscene.nodeIndices)
        {
            ecsr->make_child(root, load_node(fscene, fasset, fasset.nodes.at(fsni), ctx));
        }
    }
    else
    {
        ENG_WARN("Loaded model {} has no nodes.", filepath.string());
        return root;
    }

    ctx.root = root;
    nodes[filepath] = std::move(ctx);
    return root;
}

ecs::entity Scene::instance_entity(ecs::entity node)
{
    auto* ecs = Engine::get().ecs;
    auto* r = Engine::get().renderer;
    auto root = ecs->clone(node);
    scene.push_back(root);
    return root;
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
                p = Engine::get().ecs->get_parent(e);
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
            ImGui::Text(cmesh->name.c_str());
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
