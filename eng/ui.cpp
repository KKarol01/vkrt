#include <array>
#include <new>
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <eng/engine.hpp>
#include <eng/ui.hpp>
#include <eng/scene.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>

namespace eng
{

void UI::init()
{
    eng::Engine::get().imgui_renderer->add_ui_callback([this] {
        render();
        return true;
    });
}

void UI::update() {}

void UI::render()
{
    // ImGui::SetCurrentContext(g_ctx->imgui_ctx);
    // ImGui::SetAllocatorFunctions(alloc_callbacks.imgui_alloc, alloc_callbacks.imgui_free);

    Window* window = Engine::get().window;

    ImGui::SetNextWindowPos(ImVec2{});
    ImGui::SetNextWindowSize(ImVec2{ window->width, window->height });

    if(ImGui::Begin("main ui panel", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar))
    {
        // if(ImGui::BeginMenuBar())
        //{
        //     ImGui::MenuItem("LOLOLOL", "CTRL-S");
        //     ImGui::EndMenuBar();
        // }
        // const auto render_output_size = ImGui::GetContentRegionMax() - ImVec2{ 256.0f, 230.0f };
        //// used primarily for render output
        // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
        // if(ImGui::BeginChild("render output panel", render_output_size, ImGuiChildFlags_Border))
        //{
        //     ImGui::Image(get_imgui_render_output_descriptor(), render_output_size);
        // }
        // ImGui::EndChild();
        // ImGui::PopStyleVar();

        // const auto render_output_next_row = ImGui::GetCursorScreenPos();
        // ImGui::SameLine();
        //// used for scene manipulation
        // if(ImGui::BeginChild("right vertical panel", {}))
        //{
        //     const auto scene_panel_height = ImGui::GetContentRegionAvail().y * 0.5f;
        //     const auto scene_panel_next_row = ImGui::GetCursorScreenPos();
        //     if(ImGui::BeginChild("scene panel", { 0.0f, scene_panel_height }, ImGuiChildFlags_Border))
        //     {
        //         /*for(auto& e : Engine::get().scene->scene) {
        //             draw_scene_instance_tree(e);
        //         }*/
        //     }
        //     ImGui::EndChild();

        //    {
        //        // TODO: apply invisible button to regulate the height of the panels
        //        ImGui::Separator();
        //    }

        //    // ImGui::SetCursorScreenPos({ scene_panel_next_row.x, ImGui::GetCursorScreenPos().y });
        //    if(ImGui::BeginChild("actor panel", {}, ImGuiChildFlags_Border)) { ImGui::Text("adddsd"); }
        //    ImGui::EndChild();
        //}
        // ImGui::EndChild();

        //// used primarily for debug output
        // ImGui::SetCursorScreenPos(render_output_next_row);
        // if(ImGui::BeginChild("engine panel", { render_output_size.x, 0.0f }, ImGuiChildFlags_Border))
        //{
        //     if(ImGui::BeginTabBar("asd"))
        //     {
        //         for(const auto& t : tabs)
        //         {
        //             if(ImGui::BeginTabItem(t.name.c_str()))
        //             {
        //                 t.cb();
        //                 ImGui::EndTabItem();
        //             }
        //         }
        //         ImGui::EndTabBar();
        //     }
        // }
        // ImGui::EndChild();
    }
    ImGui::End();

    // ImGui::Render();
}

} // namespace eng