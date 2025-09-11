#include <Windows.h>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <eng/engine.hpp>
#include <eng/camera.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/scene.hpp>
#include <eng/ui.hpp>
#include <eng/ecs/ecs.hpp>

static void on_mouse_move(GLFWwindow* window, double px, double py)
{
    eng::Engine::get().window->on_mouse_move(static_cast<float>(px), static_cast<float>(py));
}

static void on_window_resize(GLFWwindow* window, int w, int h)
{
    eng::Engine::get().window->on_resize(static_cast<float>(w), static_cast<float>(h));
}

static void on_window_focus(GLFWwindow* window, int focus) { eng::Engine::get().window->on_focus(focus > 0); }

static void eng_ui_reload_dll(HMODULE hnew)
{
    // UI _ui{ .init = (eng_ui_init_t)GetProcAddress(hnew, "eng_ui_init"),
    //         .update = (eng_ui_update_t)GetProcAddress(hnew, "eng_ui_update") };
    //// TODO: transition data
    // UIContext context{
    //     .engine = &Engine::get(),
    //     .imgui_ctx = nullptr,
    //     .alloc_cbs = { malloc, free },
    // };
    //_ui.context = _ui.init(Engine::get().ui.context ? Engine::get().ui.context : &context);
    // Engine::get().ui = _ui;
}

// static void eng_vkrenderer_reload_dll(HMODULE hnew) {
//     UI _ui{ .init = (eng_ui_init_t)GetProcAddress(hnew, "eng_ui_init"),
//             .update = (eng_ui_update_t)GetProcAddress(hnew, "eng_ui_update") };
//     // TODO: transition data
//     UIContext context{
//         .engine = &Engine::get(),
//         .imgui_ctx = nullptr,
//         .alloc_cbs = { malloc, free },
//     };
//     _ui.context = _ui.init(Engine::get().ui.context ? Engine::get().ui.context : &context);
//     Engine::get().ui = _ui;
// }

// static void load_dll(const std::filesystem::path& path_dll, auto cb_dll_load_transfer_free)
//{
//     if(!std::filesystem::exists(path_dll)) { return; }
// }

namespace eng
{

Window::Window(float width, float height) : width(width), height(height) {}

Window::~Window()
{
    if(window) { glfwDestroyWindow(window); }
}

void Window::init()
{
    window = glfwCreateWindow(width, height, "window title", nullptr, nullptr);
    if(!window) { ENG_ERROR("Could not create glfw window"); }
}

bool Window::should_close() const { return glfwWindowShouldClose(window); }

void Window::on_focus(bool focus)
{
    for(auto& e : on_focus_callbacks)
    {
        const auto ret = e(focus);
        assert(ret && "Implement unsubscribing.");
    }
    focused = focus;
}

void Window::on_resize(float w, float h)
{
    for(auto& e : on_resize_callbacks)
    {
        const auto ret = e(w, h);
        assert(ret && "Implement unsubscribing.");
    }
    width = w;
    height = h;
}

void Window::on_mouse_move(float x, float y)
{
    for(auto& e : on_mouse_move_callbacks)
    {
        const auto ret = e(x, y);
        assert(ret && "Implement unsubscribing.");
    }
}

void Window::add_on_focus(const on_focus_cb_t& a) { on_focus_callbacks.push_back(a); }

void Window::add_on_resize(const on_resize_cb_t& a) { on_resize_callbacks.push_back(a); }

void Window::add_on_mouse_move(const on_mouse_move_cb_t& a) { on_mouse_move_callbacks.push_back(a); }

Engine& Engine::get()
{
    static Engine _engine;
    return _engine;
}

void Engine::init()
{
    if(!glfwInit()) { ENG_WARN("Could not initialize GLFW"); }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = new Window{ 1280.0f, 768.0f };
    ecs = new ecs::Registry{};
    renderer = new gfx::Renderer{};
    ui = new eng::UI{};
    scene = new eng::Scene{};

    window->init();
    glfwSetCursorPosCallback(window->window, on_mouse_move);
    glfwSetFramebufferSizeCallback(window->window, on_window_resize);
    glfwSetWindowFocusCallback(window->window, on_window_focus);
    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }
    camera = new Camera{ glm::radians(90.0f), 0.1f, 15.0f };
    renderer->init(new gfx::RendererBackendVulkan{});
    scene->init();
    ui->init();
}

void Engine::destroy() { this->~Engine(); }

void Engine::start()
{
    on_init.signal();
    while(!window->should_close())
    {
        if(get_time_secs() - last_frame_time >= refresh_rate)
        {
            const float now = get_time_secs();
            on_update.signal();
            camera->update();
            ui->update();
            scene->update();
            renderer->update();
            ++frame_num;
            last_frame_time = now;
        }
        delta_time = get_time_secs() - last_frame_time;
        glfwPollEvents();
    }
}

double Engine::get_time_secs() { return glfwGetTime(); }

void FrameTime::update()
{
    float time = static_cast<float>(glfwGetTime());
    float dt = time - last_time;
    last_time = time;
    tick_sum -= measures[index];
    tick_sum += dt;
    measures[index] = dt;
    index = (index + 1) % 100;
}

} // namespace eng