#pragma once

#include <memory>
#include <cstdint>
#include <deque>
#include <functional>
#include "./ui.hpp"
#include <eng/renderer/renderer.hpp>
#include <eng/camera.hpp>
#include <eng/scene.hpp>
#include <eng/ui.hpp>
#include "./ecs.hpp"

struct GLFWwindow;

struct Window {
    Window(float width, float height);
    ~Window();

    bool should_close() const;

    float width;
    float height;
    GLFWwindow* window{ nullptr };
};

struct FrameTime {
    void update();
    float get_avg_frame_time() const { return tick_sum * 0.01f; }

    float last_time{};
    float tick_sum{};
    float measures[100]{};
    int index = 0;
};

class Engine {
  public:
    void init();
    void destroy();
    void start();
    void update();

    void set_on_update_callback(const std::function<void()>& on_update_callback);
    void add_on_window_resize_callback(const std::function<bool()>& on_update_callback);
    void add_on_window_focus_callback(const std::function<void()>& on_focus);
    void notify_on_window_resize();
    void notify_on_window_focus();

    static Engine& get();
    Window* window{};
    Camera* camera{};
    components::Storage* ecs_storage{};
    gfx::Renderer* renderer{};
    eng::UI* ui{};
    scene::Scene* scene{};
    double get_time_secs();
    double last_frame_time() { return _last_frame_time; }
    double delta_time() { return _delta_time; }
    uint64_t frame_num() { return _frame_num; }

    std::deque<std::string> msg_log;

  private:
    double _last_frame_time{};
    double _delta_time{};
    uint64_t _frame_num{};
    float _refresh_rate{ 1.0f / 60.0f };
    std::function<void()> _on_update_callback;
    std::vector<std::function<bool()>> _on_window_resize_callbacks;
    std::vector<std::function<void()>> m_on_window_focus_callbacks;
};
