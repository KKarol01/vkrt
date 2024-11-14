#pragma once

#include <unordered_map>
#include <cstdint>

struct Node;

struct ImGuiCur {
    ImGuiCur();
    ImGuiCur(float x, float y);
    void get_pos();
    void get_screen_pos();
    void set_pos();
    void offset(float x, float y);
    float x;
    float y;
};

struct UIWindow {
    float x{};
    float y{};
    float w{ 25.0f };
    float h{ 150.0f };
    void get_window_pos();
    void get_window_size();
    void set_window_pos();
    void set_window_size();
};

struct NodeList {
    void draw();
    Node* selected_node{};
    std::unordered_map<const void*, bool> draw_scene_expanded;
    UIWindow window;
};

struct Console {
    void draw();
    UIWindow window;
};

struct RenderOutput {
    void draw();
    UIWindow window;
};

class UI {
  public:
    void update();

    NodeList node_list;
    Console console;
    RenderOutput routput;
};