#include "engine.hpp"
#include <array>
#include <new>
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ui.hpp"
// #include "scene.hpp"

static UIContext* g_ctx{};
static AllocatorCallbacks alloc_callbacks{};
ENG_OVERRIDE_STD_NEW_DELETE(alloc_callbacks);

static ImTextureID get_imgui_render_output_descriptor();
static void draw_scene_instance_tree(scene::NodeInstance* i);
static void draw_render_mesh(scene::NodeInstance* i);

void eng_ui_init(UIInitData* data) {
    alloc_callbacks = data->callbacks;
    if(!g_ctx) { g_ctx = new UIContext{}; }
    g_ctx->engine = data->engine;
    g_ctx->alloc_callbacks = &alloc_callbacks;
    if(data->context) { *data->context = g_ctx; }
}

void eng_ui_update() {
    auto* renderer = g_ctx->engine->renderer;
    ImGui::SetCurrentContext(g_ctx->imgui_ctx);
    ImGui::SetAllocatorFunctions(alloc_callbacks.imgui_alloc, alloc_callbacks.imgui_free);
    ImGui::SetNextWindowPos(ImVec2{});
    ImGui::SetNextWindowSize(ImVec2{ g_ctx->engine->window->width, g_ctx->engine->window->height });

    if(ImGui::Begin("main ui panel", 0,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar)) {
        if(ImGui::BeginMenuBar()) {
            ImGui::MenuItem("LOLOLOL", "CTRL-S");
            ImGui::EndMenuBar();
        }
        const auto render_output_size = ImGui::GetContentRegionMax() - ImVec2{ 256.0f, 230.0f };
        // used primarily for render output
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
        if(ImGui::BeginChild("render output panel", render_output_size, ImGuiChildFlags_Border)) {
            ImGui::Image(get_imgui_render_output_descriptor(), render_output_size);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        const auto render_output_next_row = ImGui::GetCursorScreenPos();
        ImGui::SameLine();
        // used for scene manipulation
        if(ImGui::BeginChild("right vertical panel", {})) {
            const auto scene_panel_height = ImGui::GetContentRegionAvail().y * 0.5f;
            const auto scene_panel_next_row = ImGui::GetCursorScreenPos();
            if(ImGui::BeginChild("scene panel", { 0.0f, scene_panel_height }, ImGuiChildFlags_Border)) {
                /*for(auto& e : Engine::get().scene->scene) {
                    draw_scene_instance_tree(e);
                }*/
            }
            ImGui::EndChild();

            {
                // TODO: apply invisible button to regulate the height of the panels
                ImGui::Separator();
            }

            // ImGui::SetCursorScreenPos({ scene_panel_next_row.x, ImGui::GetCursorScreenPos().y });
            if(ImGui::BeginChild("actor panel", {}, ImGuiChildFlags_Border)) { ImGui::Text("adddsd"); }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // used primarily for debug output
        ImGui::SetCursorScreenPos(render_output_next_row);
        if(ImGui::BeginChild("engine panel", { render_output_size.x, 0.0f }, ImGuiChildFlags_Border)) {
            ImGui::Image(renderer->get_imgui_texture_id(renderer->get_vsm_data().dir_light_page_table_rgb8,
                                                        ImageFilter::LINEAR, ImageAddressing::CLAMP),
                         { 128.0f, 128.0 });
            if(ImGui::SliderFloat3("debug dir light", g_ctx->engine->scene->debug_dir_light_dir, -1.0f, 1.0f)) {
                glm::vec3 v;
                memcpy(&v, g_ctx->engine->scene->debug_dir_light_dir, sizeof(v));
                *((glm::vec3*)g_ctx->engine->scene->debug_dir_light_dir) = glm::normalize(v);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Render();
}

UIContext* eng_ui_get_context() { return g_ctx; }

ImTextureID get_imgui_render_output_descriptor() {
    return static_cast<ImTextureID>(g_ctx->engine->renderer->get_imgui_texture_id(g_ctx->engine->renderer->get_color_output_texture(),
                                                                                  ImageFilter::LINEAR, ImageAddressing::CLAMP));
}

void draw_scene_instance_tree(scene::NodeInstance* i) {
    return;
    /*if(!i->has_children()) {
        ImGui::Selectable(i->name.c_str());
    } else {
        if(ImGui::TreeNode(i, "%s", i->name.c_str())) {
            for(auto& e : i->children) {
                if(e) { draw_scene_instance_tree(e); }
            }
            ImGui::TreePop();
        }
    }
    for(auto& p : i->primitives) {
        if(auto* r = Engine::get().ecs_storage->try_get<components::Renderable>(p); r) {
            const auto material = Engine::get().renderer->get_material(r->material_handle);
            const auto imid = Engine::get().renderer->get_imgui_texture_id(material.textures.base_color_texture.handle,
                                                                           material.textures.base_color_texture.filter,
                                                                           material.textures.base_color_texture.addressing);
            ImGui::Image(imid, { 25.0f, 25.0f });
        }
    }*/
}

void draw_render_mesh(scene::NodeInstance* i) {}
