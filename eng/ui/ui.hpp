#pragma once

#include <string>
#include <cstdint>
#include <eng/common/callback.hpp>
#include <eng/engine.hpp>

namespace eng
{
namespace ui
{

struct Window;
class UI;

inline UI& get_ui() { return *Engine::get().ui; }

using window_id = uint32_t;

struct Window
{
    using draw_callback_type = Callback<void(Window& _this)>;
    std::string title;
    draw_callback_type draw_callback;
    window_id first_child{};
    window_id next_sibling{};
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
        windows.push_back(Window{ title, draw_callback, ~window_id{}, ~window_id{} });
        root_windows.push_back((window_id)windows.size() - 1);
        return (window_id)windows.size() - 1;
    }

    void make_child(window_id parent, window_id child)
    {
        std::erase(root_windows, child);
        auto* sibling = &get_window(parent).first_child;
        while(*sibling != ~window_id{})
        {
            sibling = &get_window(*sibling).next_sibling;
        }
        *sibling = child;
    }

    Window& get_window(window_id id) { return windows[id]; }

    void dock_window(window_id window, uint32_t* dock_id);

    void draw();

    SceneUI sceneui;

    std::vector<window_id> root_windows;
    std::vector<Window> windows;
    std::vector<std::pair<window_id, uint32_t*>> layout;
    bool redo_layout = true;

    uint32_t dock_id{ ~0u };
    uint32_t main_panel_id{ ~0u };
    uint32_t right_panel_id{ ~0u };
};

} // namespace ui
} // namespace eng