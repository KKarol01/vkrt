#pragma once

#include <memory>
#include "renderer.hpp"

#define ENG_RTERROR(message)                                                                                                               \
    throw std::runtime_error { message }

struct GLFWindow;

struct Window {
    Window(uint32_t width, uint32_t height);
    ~Window();
    uint32_t size[2]{};
    GLFWwindow* window{};
};

class Engine {
  public:
    constexpr Engine() = default;
    static void init();
    static void destroy();

    inline static Window* window() { return &*_this->_window; }
    inline static Renderer* renderer() { return &*_this->_renderer; }

  private:
    inline static std::unique_ptr<Engine> _this;
    std::unique_ptr<Window> _window;
    std::unique_ptr<Renderer> _renderer;
};
