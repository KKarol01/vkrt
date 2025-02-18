#include <Windows.h>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include "engine.hpp"
#include "renderer.hpp"
#include "renderer_vulkan.hpp"
#include "camera.hpp"

static void on_mouse_move(GLFWwindow* window, double px, double py) { Engine::get().camera->on_mouse_move(px, py); }
static void on_window_resize(GLFWwindow* window, int w, int h) {
    Engine::get().window->width = w;
    Engine::get().window->height = h;
    Engine::get().notify_on_window_resize();
}
static void on_window_focus(GLFWwindow* window, int focus) {
    if(!focus) { return; }
    Engine::get().notify_on_window_focus();
}

static void eng_ui_reload_dll(HMODULE hnew) {
    UI _ui{ .init = (eng_ui_init_t)GetProcAddress(hnew, "eng_ui_init"),
            .update = (eng_ui_update_t)GetProcAddress(hnew, "eng_ui_update") };
    // TODO: transition data
    UIContext context{
        .engine = &Engine::get(),
        .imgui_ctx = nullptr,
        .alloc_cbs = { malloc, free },
    };
    _ui.context = _ui.init(Engine::get().ui.context ? Engine::get().ui.context : &context);
    Engine::get().ui = _ui;
}

static void load_dll(const std::filesystem::path& path_dll, bool use_hot_reload, auto cb_dll_load_transfer_free) {
    if(!std::filesystem::exists(path_dll)) { return; }
    if(!use_hot_reload) {
        HMODULE dll = LoadLibraryA(path_dll.string().c_str());
        if(!dll) {
            ENG_WARN("Cannot load dll {} because (GetLastError(): {})", path_dll.string(), GetLastError());
            return;
        }
        cb_dll_load_transfer_free(dll);
        return;
    }
    const auto path_noext = std::filesystem::path{ path_dll }.replace_extension();
    const std::filesystem::path paths[]{ std::filesystem::path{ path_noext }.concat("1.dll"),
                                         std::filesystem::path{ path_noext }.concat("2.dll") };
    uint32_t src_idx = 0, dst_idx = 1;
    HMODULE dlls[]{ GetModuleHandleA(paths[src_idx].string().c_str()), GetModuleHandleA(paths[dst_idx].string().c_str()) };
    if(!dlls[0] && !dlls[1]) {
        Engine::get().add_on_window_focus_callback([=] { load_dll(path_dll, true, cb_dll_load_transfer_free); });
    }
    if(dlls[dst_idx]) {
        dst_idx = 0;
        src_idx = 1;
    }
    if(dlls[src_idx] && std::filesystem::last_write_time(path_dll) <= std::filesystem::last_write_time(paths[src_idx])) {
        return;
    }
    if(!std::filesystem::copy_file(path_dll, paths[dst_idx], std::filesystem::copy_options::overwrite_existing)) {
        ENG_WARN("Failed copying file {} to {}", path_dll.string(), paths[dst_idx].string());
        return;
    }
    dlls[dst_idx] = LoadLibrary(paths[dst_idx].string().c_str());
    if(!dlls[0] && !dlls[1]) {
        ENG_WARN("Could not load library {}; Winapi error: {}", paths[dst_idx].string(), GetLastError());
        std::filesystem::remove(paths[dst_idx]);
        return;
    }
    cb_dll_load_transfer_free(dlls[dst_idx]);
    if(dlls[src_idx]) {
        FreeLibrary(dlls[src_idx]);
        std::filesystem::remove(paths[src_idx]);
    }
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

    window = new Window{ 1280.0f, 768.0f };
    camera = new Camera{ glm::radians(90.0f), 0.01f, 100.0f };
    ecs_storage = new components::Storage{};

    glfwSetCursorPosCallback(window->window, on_mouse_move);
    glfwSetFramebufferSizeCallback(window->window, on_window_resize);
    glfwSetWindowFocusCallback(window->window, on_window_focus);
    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { _refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }

    renderer = new RendererVulkan{};
    scene = new scene::Scene{};
    load_dll("eng_ui.dll", true, eng_ui_reload_dll);
    renderer->init();
}

void Engine::destroy() { this->~Engine(); }

void Engine::start() {
    while(!window->should_close()) {
        if(get_time_secs() - last_frame_time() >= _refresh_rate) { update(); }
        glfwPollEvents();
    }
}

void Engine::update() {
    const float now = get_time_secs();
    if(_on_update_callback) { _on_update_callback(); }
    camera->update();
    // ui.update();
    renderer->update();
    ++_frame_num;
    _last_frame_time = now;
    _delta_time = get_time_secs() - _last_frame_time;
}

void Engine::set_on_update_callback(const std::function<void()>& on_update_callback) {
    _on_update_callback = on_update_callback;
}

void Engine::add_on_window_resize_callback(const std::function<bool()>& on_update_callback) {
    _on_window_resize_callbacks.push_back(on_update_callback);
}

void Engine::add_on_window_focus_callback(const std::function<void()>& on_focus) {
    m_on_window_focus_callbacks.push_back(on_focus);
}

void Engine::notify_on_window_resize() {
    for(const auto& e : _on_window_resize_callbacks) {
        e();
    }
}

void Engine::notify_on_window_focus() {
    for(auto& e : m_on_window_focus_callbacks) {
        e();
    }
}

Engine& Engine::get() {
    static Engine _engine;
    return _engine;
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