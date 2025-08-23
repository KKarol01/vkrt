#pragma once
#pragma once

#include <memory>
#include <cstdint>
#include <deque>
#include <string>
#include <eng/common/callback.hpp>

struct GLFWwindow;

namespace eng
{

struct Window;
class Camera;
class UI;
class Scene;

namespace ecs
{
class Registry;
}

namespace gfx
{
class Renderer;
class ImGuiRenderer;
} // namespace gfx

struct Window
{
    using on_focus_cb_t = std::function<bool(bool)>;
    using on_resize_cb_t = std::function<bool(float, float)>;
    using on_mouse_move_cb_t = std::function<bool(float, float)>;

    Window(float width, float height);
    ~Window();

    void init();
    bool should_close() const;

    void on_focus(bool focus);
    void on_resize(float w, float h);
    void on_mouse_move(float x, float y);

    void add_on_focus(const on_focus_cb_t& a);
    void add_on_resize(const on_resize_cb_t& a);
    void add_on_mouse_move(const on_mouse_move_cb_t& a);

    float width{};
    float height{};
    bool focused{};
    GLFWwindow* window{ nullptr };
    std::vector<on_focus_cb_t> on_focus_callbacks;
    std::vector<on_resize_cb_t> on_resize_callbacks;
    std::vector<on_mouse_move_cb_t> on_mouse_move_callbacks;
};

struct FrameTime
{
    void update();
    float get_avg_frame_time() const { return tick_sum * 0.01f; }

    float last_time{};
    float tick_sum{};
    float measures[100]{};
    int index = 0;
};

class Engine
{
  public:
    static Engine& get();

    void init();
    void destroy();
    void start();

    Window* window{};
    Camera* camera{};
    ecs::Registry* ecs{};
    gfx::Renderer* renderer{};
    gfx::ImGuiRenderer* imgui_renderer{};
    UI* ui{};
    Scene* scene{};

    double get_time_secs();
    double last_frame_time{};
    double delta_time{};
    uint64_t frame_num{};
    float refresh_rate{ 1.0f / 60.0f };

    Signal<void()> on_init;
    Signal<void()> on_update;

    std::deque<std::string> msg_log;
};

} // namespace eng