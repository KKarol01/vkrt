#pragma once

#include <memory>
#include <print>
#include <cstdint>
#include <deque>
#include <functional>
#include "ui.hpp"
#include "renderer.hpp"
#include "camera.hpp"
#include "scene.hpp"
#include "ecs.hpp"

#ifndef NDEBUG
#define ENG_WARN(fmt, ...) std::println("[WARN][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);

#define ENG_LOG(fmt, ...)                                                                                              \
    do {                                                                                                               \
        const std::string str = std::format("[LOG][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);                  \
        std::println("{}", str);                                                                                       \
        if(Engine::get()->msg_log.size() >= 512) { Engine::get()->msg_log.pop_back(); }                                \
        Engine::get()->msg_log.push_front(str);                                                                        \
    } while(0)

#define ENG_ASSERT(expr, fmt, ...)                                                                                     \
    if(!(expr)) {                                                                                                      \
        std::println("[ASSERT][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);                                      \
        assert(false);                                                                                                 \
    }
#else
#define ENG_WARN(fmt, ...)
#define ENG_LOG(fmt, ...)
#define ENG_ASSERT(expr, fmt, ...)
#endif

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
    static void init();
    static void destroy();
    static void start();
    static void update();

    static void set_on_update_callback(const std::function<void()>& on_update_callback);
    static void add_on_window_resize_callback(const std::function<bool()>& on_update_callback);
    static void notify_on_window_resize();

    static Engine* get();
    static Window* window();
    static Camera* camera();
    static Renderer* renderer();
    static UI* ui();
    static scene::Scene* scene();
    //static EntityComponents* ec();
    static double get_time_secs();
    static double last_frame_time() { return get()->_last_frame_time; }
    static double delta_time() { return get()->_delta_time; }
    static uint64_t frame_num() { return get()->_frame_num; }

    std::deque<std::string> msg_log;

  private:
    /*static std::unique_ptr<Engine> self;
    std::unique_ptr<Window> _window;
    std::unique_ptr<Camera> _camera;
    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<UI> _ui;
    std::unique_ptr<Scene> _scene;*/

    double _last_frame_time{};
    double _delta_time{};
    uint64_t _frame_num{};
    float _refresh_rate{ 60.0f };
    std::function<void()> _on_update_callback;
    std::vector<std::function<bool()>> _on_window_resize_callbacks;
};
