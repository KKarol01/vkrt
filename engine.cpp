#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include "engine.hpp"
#include "renderer.hpp"
#include "renderer_vulkan.hpp"
#include "camera.hpp"

static void on_mouse_move(GLFWwindow* window, double px, double py) { Engine::camera()->on_mouse_move(px, py); }
static void on_window_resize(GLFWwindow* window, int w, int h) {
    Engine::window()->width = w;
    Engine::window()->height = h;
    Engine::renderer()->set_screen_rect({ .width = (uint32_t)w, .height = (uint32_t)h });
    Engine::camera()->update_projection(glm::perspectiveFov(glm::radians(90.0f), (float)w, (float)h, 0.0f, 10.0f));
    Engine::notify_on_window_resize();
}

Window::Window(uint32_t width, uint32_t height) : width(width), height(height) {
    if(!Engine::window()) {
        if(glfwInit() != GLFW_TRUE) {
            ENG_WARN("Could not initialize GLFW");
            return;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    window = glfwCreateWindow(width, height, "window title", nullptr, nullptr);

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

    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { _this->_refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }

    glfwSetCursorPosCallback(_this->_window->window, on_mouse_move);
    glfwSetFramebufferSizeCallback(_this->_window->window, on_window_resize);

    _this->_camera = std::make_unique<Camera>(glm::radians(90.0f), 0.01f, 100.0f);
    _this->_renderer = std::make_unique<RendererVulkan>();
    _this->_renderer->init();
    _this->_ui = std::make_unique<UI>();
    _this->_last_frame_time = get_time_secs();
}

void Engine::destroy() { _this.reset(); }

void Engine::start() {
    while(!Engine::window()->should_close()) {
        if(get_time_secs() - last_frame_time() >= _this->_refresh_rate) { update(); }
        glfwPollEvents();
    }
}

void Engine::update() {
    const float now = get_time_secs();
    _this->_on_update_callback();
    _this->_camera->update();
    _this->_ui->update();
    _this->_renderer->render();
    ++_this->_frame_num;
    _this->_last_frame_time = now;
    _this->_delta_time = get_time_secs() - _this->_last_frame_time;
}

void Engine::set_on_update_callback(const std::function<void()>& on_update_callback) {
    _this->_on_update_callback = on_update_callback;
}

void Engine::add_on_window_resize_callback(const std::function<bool()>& on_update_callback) {
    _this->_on_window_resize_callbacks.push_back(on_update_callback);
}

void Engine::notify_on_window_resize() {
    for(const auto& e : _this->_on_window_resize_callbacks) {
        e();
    }
}

double Engine::get_time_secs() { return glfwGetTime(); }

void FrameTime::update() {
    float time = static_cast<float>(glfwGetTime());
    float dt = time - last_time;
    last_time = time;
    tick_sum -= measures[index];
    tick_sum += dt;
    measures[index] = dt;
    index = (index + 1) % 100;
}
