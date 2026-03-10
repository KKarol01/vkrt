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
        if(ImGui::Begin("Main Panel", 0, ImGuiWindowFlags_NoMove))
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
        if(ImGui::Begin("Inspector Panel", 0)) {}
        ImGui::End();
    }
};

class ScenePanel
{
  public:
    ScenePanel(UI& ui)
    {
        auto mpwid = ui.make_window("Scene Panel", [this](gfx::RGBuilder& rg) { draw(rg); });
        ui.dock_window(mpwid, &ui.right_panel_id);
    }

    void draw(gfx::RGBuilder& rg)
    {
        if(ImGui::Begin("Scene Panel", 0))
        {
            const auto toggle_expanded_below = [this](ecs::EntityId e, bool state) {
                get_engine().ecs->traverse_hierarchy(e, [this, state](ecs::EntityId e) { states[e].expanded = state; });
            };
            const auto draw_node = [this, &toggle_expanded_below](const auto& self, ecs::EntityId e) {
                auto* ecs = get_engine().ecs;
                const auto& node = ecs->get<ecs::Node>(e);
                auto& state = states[e];
                ImGui::PushID(&node);
                {
                    auto& style = ImGui::GetStyle();
                    // height that matches arrow button
                    float row_height = style.FramePadding.y / 2 + ImGui::GetTextLineHeight();
                    // align x-left, y-middle, otherwise y is bottom and looks bad
                    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.0f, 0.5f });
                    if(ImGui::ArrowButton("##arrow", state.expanded ? ImGuiDir_Down : ImGuiDir_Right))
                    {
                        state.expanded = !state.expanded;
                    }
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - style.ItemSpacing.x / 2); // move back item spacing so it neatly touches the arrow
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - style.ItemSpacing.y / 2 + style.FramePadding.y / 2); // move up so it is flush with arrow

                    bool selected{};
                    bool preclick_value = state.expanded;
                    if(ImGui::Selectable(node.name.c_str(), &selected, 0, { 0, row_height })) {}
                    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        toggle_expanded_below(e, !state.expanded);
                    }
                    ImGui::PopStyleVar(1);
                    if(state.expanded)
                    {
                        ImGui::TreePush(&node);
                        ecs->iterate_children(e, [&self](auto e) { self(self, e); });
                        ImGui::TreePop();
                    }
                }
                ImGui::PopID();
            };
            for(auto e : get_engine().scene->scene)
            {
                draw_node(draw_node, e);
            }
        }
        ImGui::End();
    }

    struct NodeState
    {
        bool expanded{};
    };
    std::unordered_map<ecs::EntityId, NodeState> states;
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
        if(ImGui::Begin("Log Panel")) {}
        ImGui::End();
    }
};

void UI::init()
{
    auto* mainpanel = new MainPanel{ *this };           // mem leak
    auto* scenepanel = new ScenePanel{ *this };         // mem leak
    auto* inspectorpanel = new InspectorPanel{ *this }; // mem leak
    auto* logpanel = new LogPanel{ *this };             // mem leak
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
