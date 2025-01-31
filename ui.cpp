#include "engine.hpp"
#include <array>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ui.hpp"
#include "renderer_vulkan.hpp"
#include "scene.hpp"

static ImTextureID get_imgui_render_output_descriptor();
static void draw_scene_instance_tree(scene::NodeInstance* i);
static void draw_render_mesh(scene::NodeInstance* i);

void UI::update() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGui::SetNextWindowPos({});
    ImGui::SetNextWindowSize(ImVec2{ Engine::get().window->width, Engine::get().window->height });
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
                for(auto& e : Engine::get().scene->scene) {
                    draw_scene_instance_tree(e);
                }
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
        if(ImGui::BeginChild("engine panel", { render_output_size.x, 0.0f }, ImGuiChildFlags_Border)) {}
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Render();
}

ImTextureID get_imgui_render_output_descriptor() {
    return static_cast<ImTextureID>(get_renderer().get_imgui_texture_id(get_renderer().get_frame_data().gbuffer.color_image,
                                                                        ImageFilter::LINEAR, ImageAddressing::CLAMP));
}

void draw_scene_instance_tree(scene::NodeInstance* i) {
    if(!i->has_children()) {
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
    }
}

void draw_render_mesh(scene::NodeInstance* i) {}
