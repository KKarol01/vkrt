#include "engine.hpp"
#include "renderer.hpp"
#include "renderer_vulkan.hpp"

void Engine::init() {
    _this = std::make_unique<Engine>();
    _this->_renderer = std::make_unique<RendererVulkan>();
}

void Engine::destroy() { _this.reset(); }
