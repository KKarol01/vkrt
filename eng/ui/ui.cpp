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

class MainPanel
{
  public:
    MainPanel(UI& ui)
    {
        auto mpwid = ui.make_window("Main Panel", [this](gfx::RGBuilder& rg) { draw(rg); });
        ui.dock_window(mpwid, &ui.main_panel_id);
    }

    void draw(gfx::RGBuilder& rg)
    {
        ImGui::Begin("Main Panel", 0, ImGuiWindowFlags_NoMove);
        {
            const ImVec2 mpcsize = ImGui::GetContentRegionAvail();
            const float targetAspect = 16.0f / 9.0f;
            float width = mpcsize.x;
            float height = width / targetAspect;
            if(height > mpcsize.y)
            {
                height = mpcsize.y;
                width = height * targetAspect;
            }
            ImVec2 padding = { (mpcsize.x - width) * 0.5f, (mpcsize.y - height) * 0.5f };
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + padding.x, ImGui::GetCursorPosY() + padding.y));
            auto& rt = gfx::get_renderer().get_framedata().render_targets;
            auto color = rg.access_color(rg.graph->get_acc(rt.color[0]));
            ImGui::Image(*rg.graph->get_img(color), ImVec2{ width, height });
        }
        ImGui::End();
    }
};

class InspectorPanel
{
  public:
    InspectorPanel(UI& ui)
    {
        auto mpwid = ui.make_window("Inspector Panel", [this](gfx::RGBuilder& rg) { draw(rg); });
        ui.dock_window(mpwid, &ui.right_panel_id);
    }

    void draw(gfx::RGBuilder& rg)
    {
        ImGui::Begin("Inspector Panel", 0);
        {
        }
        ImGui::End();
    }
};

class LogPanel
{
  public:
    LogPanel(UI& ui)
    {
        auto mpwid = ui.make_window("Log Panel", [this](gfx::RGBuilder& rg) { draw(rg); });
        ui.dock_window(mpwid, &ui.bottom_panel_id);
    }

    void draw(gfx::RGBuilder& rg)
    {
        ImGui::Begin("Log Panel");
        {
        }
        ImGui::End();
    }
};

void UI::init()
{
    mainpanel = new MainPanel{ *this };
    inspectorpanel = new InspectorPanel{ *this };
    logpanel = new LogPanel{ *this };
    gfx::get_renderer().imgui_renderer->ui_callbacks += [this](auto& b) { draw(b); };
}

void UI::dock_window(WindowId window, uint32_t* dock_id)
{
    layout.emplace_back(window, dock_id);
    redo_layout = true;
}

void UI::draw(gfx::RGBuilder& rg)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    dock_id = ImGui::GetID("ViewportDockspace");

    if(always_redo_layout_on_start || !ImGui::DockBuilderGetNode(dock_id))
    {
        always_redo_layout_on_start = false;
        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id);
        ImGui::DockBuilderSetNodeSize(dock_id, ImGui::GetMainViewport()->Size);
        ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Down, 0.25f, &bottom_panel_id, &main_panel_id);
        ImGui::DockBuilderSplitNode(main_panel_id, ImGuiDir_Right, 0.25f, &right_panel_id, &main_panel_id);

        for(const auto& [window, id] : layout)
        {
            ImGui::DockBuilderDockWindow(get_window(window).title.c_str(), *id);
        }
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

    for(const auto& e : root_windows)
    {
        auto& window = get_window(e);
        window.draw_callback(rg);
    }
}

} // namespace ui

} // namespace eng
