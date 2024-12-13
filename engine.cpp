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

Window::Window(float width, float height) : width(width), height(height) {
    window = glfwCreateWindow(width, height, "window title", nullptr, nullptr);
    if(!window) { ENG_WARN("Could not create glfw window"); }
}

Window::~Window() {
    if(window) { glfwDestroyWindow(window); }
}

bool Window::should_close() const { return glfwWindowShouldClose(window); }

void Engine::init() {
    if(!glfwInit()) { ENG_WARN("Could not initialize GLFW"); }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwSetCursorPosCallback(window()->window, on_mouse_move);
    glfwSetFramebufferSizeCallback(window()->window, on_window_resize);
    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { get()->_refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }

    //ec()->register_component_array<cmps::Transform>();
    //ec()->register_component_array<cmps::Mesh>();
    //ec()->register_component_array<cmps::MeshInstance>();

    renderer()->init();
}

void Engine::destroy() { get()->~Engine(); }

void Engine::start() {
    while(!Engine::window()->should_close()) {
        if(get_time_secs() - last_frame_time() >= get()->_refresh_rate) { update(); }
        glfwPollEvents();
    }
}

void Engine::update() {
    const float now = get_time_secs();
    if(get()->_on_update_callback) { get()->_on_update_callback(); }
    camera()->update();
    ui()->update();
    renderer()->update();
    ++get()->_frame_num;
    get()->_last_frame_time = now;
    get()->_delta_time = get_time_secs() - get()->_last_frame_time;
}

void Engine::set_on_update_callback(const std::function<void()>& on_update_callback) {
    get()->_on_update_callback = on_update_callback;
}

void Engine::add_on_window_resize_callback(const std::function<bool()>& on_update_callback) {
    get()->_on_window_resize_callbacks.push_back(on_update_callback);
}

void Engine::notify_on_window_resize() {
    for(const auto& e : get()->_on_window_resize_callbacks) {
        e();
    }
}

Engine* Engine::get() {
    static Engine _engine;
    return &_engine;
}

Window* Engine::window() {
    static Window _window{ 1280.0f, 768.0f };
    return &_window;
}

Camera* Engine::camera() {
    static Camera _camera{ glm::radians(90.0f), 0.01f, 100.0f };
    return &_camera;
}

Renderer* Engine::renderer() {
    static Renderer* _renderer = new RendererVulkan();
    return _renderer;
}

UI* Engine::ui() {
    static UI _ui;
    return &_ui;
}

scene::Scene* Engine::scene() {
    static scene::Scene _scene;
    return &_scene;
}

//EntityComponents* Engine::ec() {
//    static EntityComponents _ec = EntityComponents{};
//    return &_ec;
//}

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