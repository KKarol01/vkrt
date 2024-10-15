#pragma once

#include <unordered_map>

struct Node;

class UI {
  public:
    void update();
    void draw_left_column();
    void draw_bottom_shelf();
    void draw_right_column();
    void set_selected(Node* ptr);

    std::unordered_map<const void*, bool> draw_scene_expanded;

    Node* draw_scene_selected{};
    float output_offset_x;
    float output_offset_y;
    float output_offset_w;
    float output_offset_h;
};