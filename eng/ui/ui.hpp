#pragma once

#include <string>
#include <cstdint>
#include <eng/common/callback.hpp>
#include <eng/common/indexed_hierarchy.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/engine.hpp>

namespace eng
{
namespace ui
{

class MainPanel;
class InspectorPanel;
class LogPanel;

struct Window;
class UI;

inline UI& get_ui() { return *get_engine().ui; }

using WindowId = TypedId<Window, uint32_t>;

struct Window
{
    using draw_callback_type = Callback<void(gfx::RGBuilder&)>;
    std::string title;
    draw_callback_type draw_callback;
};

class UI
{
  public:
    void init();

    WindowId make_window(const std::string& title, const Window::draw_callback_type& draw_callback)
    {
        const auto hnid = hierarchy.create();
        if(windows.size() == *hnid) { windows.emplace_back(title, draw_callback); }
        else { windows[*hnid] = Window{ title, draw_callback }; }
        const auto id = WindowId{ *hnid };
        root_windows.push_back(id);
        return id;
    }

    void make_child(WindowId parent_id, WindowId child_id)
    {
        std::erase(root_windows, child_id);
        hierarchy.make_child(IndexedHierarchy::NodeId{ *parent_id }, IndexedHierarchy::NodeId{ *child_id });
    }

    Window& get_window(WindowId id) { return windows[*id]; }

    void dock_window(WindowId window, uint32_t* dock_id);

    void draw(gfx::RGBuilder& b);

    IndexedHierarchy hierarchy;
    std::vector<WindowId> root_windows;
    std::vector<Window> windows;
    std::vector<std::pair<WindowId, uint32_t*>> layout; // todo: get rid of the pointer...
    inline static bool always_redo_layout_on_start = true;
    bool redo_layout = true;

    uint32_t dock_id{ ~0u };
    uint32_t main_panel_id{ ~0u };
    uint32_t right_panel_id{ ~0u };
    uint32_t bottom_panel_id{ ~0u };

    MainPanel* mainpanel{};
    InspectorPanel* inspectorpanel{};
    LogPanel* logpanel{};
};

} // namespace ui
} // namespace eng