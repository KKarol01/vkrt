#pragma once

#include "scene.hpp"
#include <unordered_map>

class UI {
  public:
    void update();
    void draw_left_column();
    void draw_bottom_shelf();
    void draw_right_column();
    void set_selected(Scene::MeshInstance* ptr);

    std::unordered_map<const void*, bool> draw_scene_expanded;

    enum SelectedType { MESH_INSTANCE };
    void* draw_scene_selected{};
    SelectedType draw_scene_selected_type;
};