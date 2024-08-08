#pragma once

#include <memory>
#include <print>
#include <cstdint>
#include "renderer.hpp"

#ifndef NDEBUG
#define ENG_RTERROR(fmt, ...)                                                                                          \
    throw std::runtime_error { std::format("[RT ERROR][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__) }

#define ENG_WARN(fmt, ...) std::println("[WARN][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);

#define ENG_LOG(fmt, ...) std::println("[LOG][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);
#else
#define ENG_RTERROR(fmt, ...)
#define ENG_WARN(fmt, ...)
#define ENG_LOG(fmt, ...)
#endif

struct GLFWwindow;

struct Window {
    Window(uint32_t width, uint32_t height);
    ~Window();
    uint32_t size[2]{};
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
    constexpr Engine() = default;
    static void init();
    static void destroy();

    inline static Window* window() { return &*_this->_window; }
    inline static Renderer* renderer() { return &*_this->_renderer; }

  private:
    static std::unique_ptr<Engine> _this;
    std::unique_ptr<Window> _window;
    std::unique_ptr<Renderer> _renderer;
};
