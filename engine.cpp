#include <GLFW/glfw3.h>
#include "engine.hpp"
#include "renderer.hpp"
#include "renderer_vulkan.hpp"

Window::Window(uint32_t width, uint32_t height) {
    if(!Engine::window()) {
        if(glfwInit() != GLFW_TRUE) { ENG_RTERROR("Could not initialize GLFW"); }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    size[0] = width;
    size[1] = height;
    window = glfwCreateWindow(size[0], size[1], "window title", nullptr, nullptr);

    if(!window) { ENG_RTERROR("Could not create glfw window"); }
}

Window::~Window() {
    if(window) { glfwDestroyWindow(window); }
    if(!Engine::window()) { glfwTerminate(); }
}

Engine::~Engine() = default;

void Engine::init() {
    _this = std::make_unique<Engine>();

    _this->_window = std::make_unique<Window>(1280, 768);
    _this->_renderer = std::make_unique<RendererVulkan>();
    _this->_renderer->init();
}

void Engine::destroy() { _this.reset(); }

std::unique_ptr<Engine> Engine::_this = {};