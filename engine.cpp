#include <GLFW/glfw3.h>
#include "engine.hpp"
#include "renderer.hpp"
#include "renderer_vulkan.hpp"
#include "camera.hpp"

Window::Window(uint32_t width, uint32_t height) {
    if(!Engine::window()) {
        if(glfwInit() != GLFW_TRUE) {
            ENG_WARN("Could not initialize GLFW");
            return;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    size[0] = width;
    size[1] = height;
    window = glfwCreateWindow(size[0], size[1], "window title", nullptr, nullptr);

    if(!window) { ENG_WARN("Could not create glfw window"); }
}

Window::~Window() {
    if(window) { glfwDestroyWindow(window); }
    if(!Engine::window()) { glfwTerminate(); }
}

bool Window::should_close() const { return glfwWindowShouldClose(window); }

void Engine::init() {
    _this = std::make_unique<Engine>();

    _this->_window = std::make_unique<Window>(1280, 768);
    _this->_camera = std::make_unique<Camera>(glm::radians(90.0f), 0.01f, 100.0f);
    _this->_renderer = std::make_unique<RendererVulkan>();
    _this->_renderer->init();

    _this->_last_frame_time = get_time_secs();

    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { _this->_refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }
}

void Engine::destroy() { _this.reset(); }

void Engine::start() {
    while(!Engine::window()->should_close()) {
        if(get_time_secs() - last_frame_time() >= _this->_refresh_rate) {
            update();
        }
        glfwPollEvents();
    }
}

void Engine::update() {
    _this->_delta_time = get_time_secs() - _this->_last_frame_time;
    _this->_last_frame_time = get_time_secs();
    _this->_on_update_callback();
    _this->_camera->update();
    _this->_renderer->render();
    ++_this->_frame_num;
}

void Engine::set_on_update_callback(const std::function<void()>& on_update_callback) {
    _this->_on_update_callback = on_update_callback;
}

double Engine::get_time_secs() { return glfwGetTime(); }

std::unique_ptr<Engine> Engine::_this = {};

void FrameTime::update() {
    float time = static_cast<float>(glfwGetTime());
    float dt = time - last_time;
    last_time = time;
    tick_sum -= measures[index];
    tick_sum += dt;
    measures[index] = dt;
    index = (index + 1) % 100;
}
