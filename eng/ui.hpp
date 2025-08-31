#pragma once

#include <functional>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "./common/dll_hot_reload.hpp"

namespace eng
{

class UI
{
    using cb_func_t = Callback<void()>;

  public:
    enum class Location
    {
        NEW_PANE,
        LEFT_PANE,
        RIGHT_PANE,
        BOTTOM_PANE,
        CENTER_PANE,
    };

    struct Tab
    {
        std::string name;
        Location location{ Location::NEW_PANE };
        cb_func_t cb_func{};
    };

    inline static bool use_default_layout = false;

    void init();
    void update();
    void add_tab(const Tab& t) { tabs.emplace_back(t); }
    std::vector<Tab> tabs;
    uint32_t viewport_imid;
    uint32_t left_imid;
    uint32_t right_imid;
    uint32_t bottom_imid;
};

} // namespace eng