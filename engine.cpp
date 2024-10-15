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
    self = std::make_unique<Engine>();

    self->_window = std::make_unique<Window>(1280, 768);

    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { self->_refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }

    glfwSetCursorPosCallback(self->_window->window, on_mouse_move);
    glfwSetFramebufferSizeCallback(self->_window->window, on_window_resize);

    self->_camera = std::make_unique<Camera>(glm::radians(90.0f), 0.01f, 100.0f);
    self->_renderer = std::make_unique<RendererVulkan>();
    self->_renderer->init();
    self->_ui = std::make_unique<UI>();
    self->_scene = std::make_unique<Scene>();
    self->_last_frame_time = get_time_secs();

    ec()->register_component_array<cmps::Transform>();
    ec()->register_component_array<cmps::RenderMesh>();
}

void Engine::destroy() { self.reset(); }

void Engine::start() {
    while(!Engine::window()->should_close()) {
        if(get_time_secs() - last_frame_time() >= self->_refresh_rate) { update(); }
        glfwPollEvents();
    }
}

void Engine::update() {
    const float now = get_time_secs();
    self->_on_update_callback();
    self->_camera->update();
    self->_ui->update();
    self->_renderer->render();
    ++self->_frame_num;
    self->_last_frame_time = now;
    self->_delta_time = get_time_secs() - self->_last_frame_time;
}

void Engine::set_on_update_callback(const std::function<void()>& on_update_callback) {
    self->_on_update_callback = on_update_callback;
}

void Engine::add_on_window_resize_callback(const std::function<bool()>& on_update_callback) {
    self->_on_window_resize_callbacks.push_back(on_update_callback);
}

void Engine::notify_on_window_resize() {
    for(const auto& e : self->_on_window_resize_callbacks) {
        e();
    }
}

EntityComponents* Engine::ec() {
    static EntityComponents* _ec = new EntityComponents{};
    return _ec;
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

std::unique_ptr<Engine> Engine::self = {};