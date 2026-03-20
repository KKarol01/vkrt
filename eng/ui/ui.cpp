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

class Panel
{
  public:
    Panel(std::string_view title, uint32_t* dock) : title(title), dock(dock) {}
    virtual ~Panel() noexcept = default;
    virtual void draw(gfx::RGBuilder& rg) = 0;
    std::string title;
    uint32_t* dock{};
};

class GamePanel : public Panel
{
  public:
    GamePanel(UI& ui, uint32_t* dock) : Panel("Game Panel", dock) {}

    ~GamePanel() noexcept override = default;

    void draw(gfx::RGBuilder& rg) override
    {
        if(ImGui::Begin("Game Panel", 0, ImGuiWindowFlags_NoMove))
        {
            ImVec2 mpcsize = ImGui::GetContentRegionAvail();
            if(mpcsize.x <= 0.0f || mpcsize.y <= 0.0f)
            {
                mpcsize = ImVec2{ gfx::get_renderer().settings.render_resolution.x,
                                  gfx::get_renderer().settings.render_resolution.y };
            }
            const float targetAspect = 16.0f / 9.0f;
            float width = mpcsize.x;
            float height = width / targetAspect;
            if(height > mpcsize.y)
            {
                height = mpcsize.y;
                width = height * targetAspect;
            }
            width = std::floor(width);
            height = std::floor(height);
            gfx::get_renderer().settings.new_render_resolution = { width, height };
            ImVec2 padding = { (mpcsize.x - width) * 0.5f, (mpcsize.y - height) * 0.5f };
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + padding.x, ImGui::GetCursorPosY() + padding.y));
            auto& rt = gfx::get_renderer().current_data->render_targets;
            auto color = rg.access_color(rg.graph->get_acc(rt.color[(int)gfx::RenderPassType::FORWARD]));
            ImGui::Image(*rg.graph->get_img(color), ImVec2{ width, height });
        }
        ImGui::End();
    }
};

class ScenePanel : public Panel
{
  public:
    ScenePanel(UI& ui, uint32_t* dock) : Panel("Scene Panel", dock) {}

    ~ScenePanel() noexcept override = default;

    void draw(gfx::RGBuilder& rg) override
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
                    {
                        // align x-left, y-middle, otherwise y is bottom and looks bad
                        if(ecs->has_children(e))
                        {
                            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.0f, 0.5f });
                            if(ImGui::ArrowButton("##arrow", state.expanded ? ImGuiDir_Down : ImGuiDir_Right))
                            {
                                state.expanded = !state.expanded;
                            }
                            ImGui::SameLine();
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - style.ItemSpacing.x / 2); // move back item spacing so it neatly touches the arrow
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - style.ItemSpacing.y / 2 + style.FramePadding.y / 2); // move up so it is flush with arrow
                            ImGui::PopStyleVar(1);
                        }
                        else
                        {
                            // move right so it's exactly inline vertically with other selects that can be expanded.
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.ItemSpacing.x / 2 - style.FramePadding.x / 2);
                        }
                    }

                    bool selected = selected_node == e;
                    if(ImGui::Selectable(node.name.c_str(), &selected, 0, { 0, row_height }))
                    {
                        if(selected_node == e) { selected_node = ecs::EntityId{}; }
                        else { selected_node = e; }
                    }
                    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        toggle_expanded_below(e, !state.expanded);
                    }
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
    ecs::EntityId selected_node;
    std::unordered_map<ecs::EntityId, NodeState> states;
};

class InspectorPanel : public Panel
{
  public:
    InspectorPanel(UI& ui, ScenePanel* scene, uint32_t* dock) : Panel("Inspector Panel", dock), scene(scene) {}

    ~InspectorPanel() noexcept override = default;

    void draw(gfx::RGBuilder& rg) override
    {
        if(!scene->selected_node) { return; }
        if(ImGui::Begin("Inspector Panel", 0)) {}
        ImGui::End();
    }

    ScenePanel* scene{};
};

class ConsolePanel : public Panel
{
  public:
    ConsolePanel(UI& ui, uint32_t* dock) : Panel("Console Panel", dock) {}

    ~ConsolePanel() noexcept override = default;

    void draw(gfx::RGBuilder& rg) override
    {
        if(ImGui::Begin("Console Panel")) {}
        ImGui::End();
    }
};

void UI::init()
{
    reset_layout |= always_redo_layout_on_start;
    gfx::get_renderer().imgui_renderer->ui_callbacks += [this](auto& b) { draw(b); };

    auto& gamepanel = panels.emplace_back(new GamePanel{ *this, &game });
    auto& scenepanel = panels.emplace_back(new ScenePanel{ *this, &scene });
    auto& inspectorpanel = panels.emplace_back(new InspectorPanel{ *this, (ScenePanel*)&*scenepanel, &inspector });
    auto& consolepanel = panels.emplace_back(new ConsolePanel{ *this, &console });
}

void UI::draw(gfx::RGBuilder& rg)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    dock_id = ImGui::GetID("ViewportDockspace");

    if(reset_layout || !ImGui::DockBuilderGetNode(dock_id))
    {
        reset_layout = false;
        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id);
        ImGui::DockBuilderSetNodeSize(dock_id, ImGui::GetMainViewport()->Size);

        ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Right, 0.25f, &scene, &game);
        ImGui::DockBuilderSplitNode(game, ImGuiDir_Down, 0.25f, &console, &game);
        ImGui::DockBuilderSplitNode(scene, ImGuiDir_Down, 0.25f, &inspector, &scene);

        for(auto& e : panels)
        {
            ImGui::DockBuilderDockWindow(e->title.c_str(), *e->dock);
        }
        ImGui::DockBuilderFinish(dock_id);
    }

    ImGui::DockSpaceOverViewport(dock_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    if(ImGui::BeginMainMenuBar())
    {
        ImGui::Button("a");
        ImGui::Button("b");
        if(ImGui::Button("Save Layout")) { ImGui::SaveIniSettingsToDisk("imgui.ini"); }
        if(ImGui::Button("Reset Layout")) { reset_layout = true; }
        ImGui::EndMainMenuBar();
    }

    for(const auto& p : panels)
    {
        p->draw(rg);
    }
}

} // namespace ui

} // namespace eng
