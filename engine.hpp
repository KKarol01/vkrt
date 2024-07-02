#pragma once

#include <memory>
#include "renderer.hpp"

class Engine {
  public:
    constexpr Engine() = default;
    static void init();
    static void destroy();

    inline static Renderer* renderer() { return &*_this->_renderer; }

  private:
    inline static std::unique_ptr<Engine> _this;
    std::unique_ptr<Renderer> _renderer;
};
