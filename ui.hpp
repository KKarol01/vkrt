#pragma once

#include <unordered_map>
#include <cstdint>

#ifdef ENG_BUILD_AS_DLL
#define ENG_API_CALL(ret, name, ...)                                                                                   \
    using eng_##name##_t = ret (*)(__VA_ARGS__);                                                                       \
    extern "C" __declspec(dllexport) ret eng_##name##(__VA_ARGS__)
#define ENG_OVERRIDE_STD_NEW_DELETE(alloc_cbs)                                                                         \
    void* operator new(std::size_t size) { return alloc_cbs.alloc(size); }                                             \
    void operator delete(void* data) { alloc_cbs.free(data); }
#else
#define ENG_API_CALL(ret, name, ...) using eng_##name##_t = ret (*)(__VA_ARGS__)
#endif

struct AllocatorCallbacks {
    void* (*alloc)(size_t size);
    void (*free)(void* data);
    void* (*imgui_alloc)(size_t size, void* user_data);
    void (*imgui_free)(void* data, void* user_data);
};

class Engine;
struct ImGuiContext;

struct UIContext {
    Engine* engine;
    ImGuiContext* imgui_ctx;
    AllocatorCallbacks alloc_cbs;

    std::vector<std::string> asdf;
};

ENG_API_CALL(UIContext*, ui_init, UIContext*);
ENG_API_CALL(void, ui_update);

struct UI {
    eng_ui_init_t init;
    eng_ui_update_t update;
    UIContext* context;
};
