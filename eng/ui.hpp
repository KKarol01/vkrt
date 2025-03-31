#pragma once

#include <unordered_map>
#include <cstdint>
#include "./common/dll_hot_reload.hpp"

struct AllocatorCallbacks {
    void* (*alloc)(size_t size);
    void (*free)(void* data);
    void* (*imgui_alloc)(size_t size, void* user_data);
    void (*imgui_free)(void* data, void* user_data);
};

class Engine;
struct ImGuiContext;

struct UIContext {
    Engine* engine{};
    ImGuiContext* imgui_ctx{};
    AllocatorCallbacks* alloc_callbacks{};
};

struct UIInitData {
    Engine* engine{};
    AllocatorCallbacks callbacks{};
    UIContext** context{};
};

ENG_API_CALL void eng_ui_init(UIInitData* init_context);
ENG_API_CALL void eng_ui_update();
ENG_API_CALL UIContext* eng_ui_get_context();

