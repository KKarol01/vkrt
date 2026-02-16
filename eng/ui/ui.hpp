#pragma once

#include <string>
#include <cstdint>
#include <eng/common/callback.hpp>
#include <eng/common/indexed_hierarchy.hpp>
#include <eng/engine.hpp>

namespace eng
{
namespace ui
{

struct Window;
class UI;

inline UI& get_ui() { return *Engine::get().ui; }

using window_id = IndexedHierarchy<Window>::element_id;

struct Window
{
    using draw_callback_type = Callback<void(Window& _this)>;
    std::string title;
    draw_callback_type draw_callback;
};

class SceneUI
{
  public:
    void init();
};

class UI
{
  public:
    void init();

    window_id make_window(const std::string& title, const Window::draw_callback_type& draw_callback)
    {
        window_id window = windows.insert(title, draw_callback);
        root_windows.push_back(window);
        return window;
    }

    void make_child(window_id parent_id, window_id child_id)
    {
        std::erase(root_windows, child_id);
        windows.make_child(parent_id, child_id);
    }

    Window& get_window(window_id id) { return windows.at(id); }

    void dock_window(window_id window, uint32_t* dock_id);

    void draw();

    SceneUI sceneui;

    IndexedHierarchy<Window> windows;
    std::vector<window_id> root_windows;
    std::vector<std::pair<window_id, uint32_t*>> layout;
    bool redo_layout = true;

    uint32_t dock_id{ ~0u };
    uint32_t main_panel_id{ ~0u };
    uint32_t right_panel_id{ ~0u };
};

} // namespace ui
} // namespace eng