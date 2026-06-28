#include <Windows.h>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <eng/ui/ui.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan/vulkan_backend.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/engine.hpp>
#include <eng/camera.hpp>
#include <eng/scene.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/fs/fs.hpp>
#include <eng/assets/asset_manager.hpp>

using namespace eng;

// static usize total_mem = 0;
// static usize peak_total_mem = 0;
//
// void* operator new(std::size_t size)
//{
//     total_mem += size;
//     peak_total_mem = std::max(peak_total_mem, total_mem);
//     return std::malloc(size);
// }
//
// void operator delete(void* ptr) noexcept
//{
//     total_mem -= _msize(ptr);
//     std::free(ptr);
// }
//
// void* operator new[](std::size_t size)
//{
//     peak_total_mem = std::max(peak_total_mem, total_mem);
//     total_mem += size;
//     return std::malloc(size);
// }
//
// void operator delete[](void* ptr) noexcept
//{
//     total_mem -= _msize(ptr);
//     std::free(ptr);
// }
//
// void* operator new(std::size_t size, std::align_val_t al)
//{
//	ENG_ASSERT(false);
//	return nullptr;
//     peak_total_mem = std::max(peak_total_mem, total_mem);
//     return _aligned_malloc(size, (size_t)al);
// }
//
// void operator delete(void* ptr, std::align_val_t al) noexcept { _aligned_free(ptr); }
//
// void* operator new[](std::size_t size, std::align_val_t al)
//{
//	ENG_ASSERT(false);
//	return nullptr;
//     peak_total_mem = std::max(peak_total_mem, total_mem);
//     return _aligned_malloc(size, (size_t)al);
// }
//
// void operator delete[](void* ptr, std::align_val_t al) noexcept { _aligned_free(ptr); }

static void on_mouse_move(GLFWwindow* window, double px, double py)
{
    eng::get_engine().window->on_mouse_move(static_cast<float>(px), static_cast<float>(py));
}

static void on_window_resize(GLFWwindow* window, int w, int h)
{
    eng::get_engine().window->on_resize(static_cast<float>(w), static_cast<float>(h));
}

static void on_window_focus(GLFWwindow* window, int focus) { eng::get_engine().window->on_focus(focus > 0); }

// static void eng_ui_reload_dll(HMODULE hnew)
//{
//      UI _ui{ .init = (eng_ui_init_t)GetProcAddress(hnew, "eng_ui_init"),
//              .update = (eng_ui_update_t)GetProcAddress(hnew, "eng_ui_update") };
//     // TODO: transition data
//      UIContext context{
//          .engine = &get_engine(),
//          .imgui_ctx = nullptr,
//          .alloc_cbs = { malloc, free },
//      };
//     _ui.context = _ui.init(get_engine().ui.context ? get_engine().ui.context : &context);
//      get_engine().ui = _ui;
// }

// static void eng_vkrenderer_reload_dll(HMODULE hnew) {
//     UI _ui{ .init = (eng_ui_init_t)GetProcAddress(hnew, "eng_ui_init"),
//             .update = (eng_ui_update_t)GetProcAddress(hnew, "eng_ui_update") };
//     // TODO: transition data
//     UIContext context{
//         .engine = &get_engine(),
//         .imgui_ctx = nullptr,
//         .alloc_cbs = { malloc, free },
//     };
//     _ui.context = _ui.init(get_engine().ui.context ? get_engine().ui.context : &context);
//     get_engine().ui = _ui;
// }

// static void load_dll(const std::filesystem::path& path_dll, auto cb_dll_load_transfer_free)
//{
//     if(!std::filesystem::exists(path_dll)) { return; }
// }

namespace eng
{
ScopedTimer::ScopedTimer(size_t buf_len) : ScopedTimer(std::string_view{ g_timers.label_buf, buf_len }) {}

ScopedTimer::ScopedTimer(std::string_view label)
{
#ifdef ENG_DEBUG_BUILD
    auto& t = g_timers.timers.emplace_back();
    // push timer onto stack
    t.label = label;
    t.parent = g_timers.timer;
    if(t.parent) { t.nest_level = t.parent->nest_level + 1; }
    g_timers.timer = &t;
    timer = &t;
    t.time_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

ScopedTimer::~ScopedTimer()
{
#ifdef ENG_DEBUG_BUILD

    // calculate time delta
    const auto time =
        (size_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    timer->time_us = time - timer->time_us;

    // pop timer from the stack
    g_timers.timer = timer->parent;

    // main timer, no previous parents, print messages in-order of push sequence
    if(!timer->parent)
    {
        constexpr std::string_view tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
        for(const auto& t : g_timers.timers)
        {
            ENG_ASSERT(t.nest_level < tabs.size());
            const auto delta_ms = std::chrono::duration<float, std::milli>(std::chrono::microseconds{ t.time_us });
            ENG_LOG("{}[{: >6.2f}ms]{}", tabs.substr(0, t.nest_level), delta_ms.count(), t.label.as_view());
        }
        g_timers.timers.clear();
        ENG_ASSERT(g_timers.manually_scoped_timers.empty());
        return;
    }
#endif
}

Window::Window(float width, float height) : size((u32)width, (u32)height) {}

Window::~Window()
{
    if(window) { glfwDestroyWindow(window); }
}

void Window::init()
{
    window = glfwCreateWindow((i32)size.x, (i32)size.y, "window title", nullptr, nullptr);
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
    size = { w, h };
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

void Settings::parse_cmdline_args(int count, const char* const argv[])
{
    enum Type
    {
        TNone,
        TBool,
        TLastEnum,
    };
    struct Setting
    {
        Type type{};
        void* setting{};
        union DefaultValue {
            bool Bool;
        } value;
    };
    std::unordered_map<std::string_view, Setting> settings{ { "--no-serialize", { TBool, &serialize_to_enbc, { .Bool = false } } } };
    for(auto i = 1u; i < count; ++i)
    {
        auto it = settings.find(argv[i]);
        if(it == settings.end())
        {
            ENG_WARN("Undefined cmdline arg {}", argv[i]);
            continue;
        }
        switch(it->second.type)
        {
            static_assert((int)TLastEnum == 2);
        case TBool:
        {
            *(bool*)it->second.setting = it->second.value.Bool;
            continue;
        }
        default:
        {
            ENG_ASSERT(false, "Unhandled enum");
            continue;
        }
        }
    }
}

void Engine::init(int argc, char* argv[])
{
    settings.parse_cmdline_args(argc, argv);
    if(!glfwInit()) { ENG_ERROR("Could not initialize GLFW"); }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    fs = new fs::FileSystem{};
    assets = new assets::AssetManager{};
    window = new Window{ 1600.0f, 900.0f };
    ecs = new ecs::Registry{};
    renderer = new gfx::Renderer{};
    ui = new ui::UI{};
    scene = new eng::Scene{};

    if(!fs->init())
    {
        destroy();
        ENG_ERROR("Engine assets manager initialization failed.");
        return;
    }

    assets->init();
    window->init();
    glfwSetCursorPosCallback(window->window, on_mouse_move);
    glfwSetFramebufferSizeCallback(window->window, on_window_resize);
    glfwSetWindowFocusCallback(window->window, on_window_focus);
    const GLFWvidmode* monitor_videomode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(monitor_videomode) { refresh_rate = 1.0f / static_cast<float>(monitor_videomode->refreshRate); }

    camera = new Camera{ glm::radians(90.0f), 0.1f };
    renderer->init(new gfx::RendererBackendVk{});
    ui->init();

    engine_startup_time = std::chrono::steady_clock::now().time_since_epoch().count();
}

void Engine::destroy()
{
    delete scene;
    delete ui;
    delete renderer;
    delete ecs;
    delete camera;
    delete window;
    delete assets;
    delete fs;
}

void Engine::start()
{
    on_init.signal();
    while(!window->should_close())
    {
        if(get_time_secs() - last_frame_time >= refresh_rate)
        {
            // ENG_LOG("Total memory: {} | Peak: {}", (float)total_mem / (float)MiB, peak_total_mem / (float)MiB);
            const float now = get_time_secs();
            on_update.signal();
            camera->update();
            // scene->update();
            renderer->update();
            ++tick;
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
