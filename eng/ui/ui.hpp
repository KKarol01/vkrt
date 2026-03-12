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

struct Window;
class UI;

inline UI& get_ui() { return *get_engine().ui; }

using WindowId = TypedId<Window, uint32_t>;

struct Window
{
    using draw_callback_type = Callback<void(gfx::RGBuilder&)>;
    std::string title;
    draw_callback_type draw_callback;
    uint32_t dock_at{};
};

class UI
{
    struct NodeSplits
    {
        uint32_t* id{};
        uint32_t dock_ids[4]{}; // l,r,u,d (same as imguidir)
        float dock_ratios[4]{}; // must be > 0 to be split
    };

  public:
    void init();

    WindowId make_window(Window&& window)
    {
        const auto hnid = hierarchy.create();
        if(windows.size() == *hnid) { windows.push_back(std::move(window)); }
        else { windows[*hnid] = std::move(window); }
        const auto id = WindowId{ *hnid };
        root_windows.push_back(id);
        windowmap[windows[*hnid].title] = id;
        return id;
    }

    void make_child(WindowId parent_id, WindowId child_id)
    {
        std::erase(root_windows, child_id);
        hierarchy.make_child(IndexedHierarchy::NodeId{ *parent_id }, IndexedHierarchy::NodeId{ *child_id });
    }

    Window& get_window(WindowId id) { return windows[*id]; }

    void draw(gfx::RGBuilder& b);

    IndexedHierarchy hierarchy;
    std::vector<WindowId> root_windows;
    std::vector<Window> windows;
    std::unordered_map<std::string, WindowId> windowmap;
    std::deque<NodeSplits> splits;
    std::unordered_map<uint32_t*, NodeSplits*> splitmap;
    inline static bool always_redo_layout_on_start = true;

    WindowId fullscreen;
    uint32_t dock_id{};
};

} // namespace ui
} // namespace eng