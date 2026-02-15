#include "ui.hpp"

#include <third_party/imgui/imgui.h>
#include <third_party/imgui/imgui_internal.h>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>

#include <eng/scene.hpp>

namespace eng
{
namespace ui
{

void SceneUI::init()
{
    auto& ui = get_ui();

    const auto shid = ui.make_window("Scene Hierarchy", [](Window& window) {
        auto& scene = *Engine::get().scene;
        // ImGui::DockBuilderDockWindow(window.title.c_str(), get_ui().right_panel_id);
        if(ImGui::Begin(window.title.c_str(), 0, ImGuiWindowFlags_HorizontalScrollbar)) { scene.ui_draw_scene(); }
        ImGui::End();
    });
    ui.dock_window(shid, &ui.right_panel_id);
}

void UI::init()
{
    sceneui.init();
    gfx::get_renderer().imgui_renderer->ui_callbacks += [this] { draw(); };
}

void UI::dock_window(window_id window, uint32_t* dock_id)
{
    layout.emplace_back(window, dock_id);
    redo_layout = true;
}

void UI::draw()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    dock_id = ImGui::GetID("ViewportDockspace");

    if(!ImGui::DockBuilderGetNode(dock_id))
    {
        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id);
        ImGui::DockBuilderSetNodeSize(dock_id, ImGui::GetMainViewport()->Size);

        ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Right, 0.25f, &right_panel_id, &main_panel_id);

        ImGui::DockBuilderDockWindow("Main panel", main_panel_id);
        ImGui::DockBuilderDockWindow("Right panel", right_panel_id);
        // for(const auto& [window, id] : layout)
        //{
        //     ImGui::DockBuilderDockWindow(get_window(window).title.c_str(), *id);
        // }
        ImGui::DockBuilderFinish(dock_id);
    }

    {
        ImGui::DockSpaceOverViewport(dock_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    }

    if(ImGui::BeginMainMenuBar())
    {
        ImGui::Button("a");
        ImGui::Button("b");
        ImGui::EndMainMenuBar();
    }

    ImGui::Begin("Main panel", 0, ImGuiWindowFlags_NoMove);
    ImGui::End();
    ImGui::Begin("Right panel", 0);
    ImGui::End();

    // for(const auto& e : root_windows)
    //{
    //     auto& window = get_window(e);
    //     window.draw_callback(window);
    // }
}

} // namespace ui

} // namespace eng
