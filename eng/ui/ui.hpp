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
class Panel;
class UI;

inline UI& get_ui() { return *get_engine().ui; }

using PanelId = TypedId<Panel, uint32_t>;

class UI
{
    inline static bool always_redo_layout_on_start = false;

  public:
    void init();

    Panel& get_panel(PanelId id) { return *panels[*id]; }

    void draw(gfx::RGBuilder& b);

    bool reset_layout{};

    std::vector<std::shared_ptr<Panel>> panels;
    std::unordered_map<std::string, PanelId> panelmap;

    PanelId fullscreen;
    uint32_t dock_id{};
    uint32_t game{};
    uint32_t scene{};
    uint32_t console{};
    uint32_t inspector{};
};

} // namespace ui
} // namespace eng