#pragma once

#include "scene.hpp"

struct WidgetPos {
    int x{}, y{};
    int w{}, h{};
};

class Widget {
  public:
    Widget() = default;
    Widget(WidgetPos pos) : pos{ pos } {}
    virtual void draw() = 0;
    WidgetPos pos;
};

class InstancedModelListWidget : public Widget {
  public:
    void draw() final;
    void draw_selectable_expandable_label(const Scene::ModelInstance& mi);

    const void* selected{};
    std::unordered_map<Handle<Scene::ModelInstance>, bool> is_expanded;
};

class RenderOutputWidget : public Widget {
  public:
    void draw() final;
};

class EngineLogWidget : public Widget {
  public:
    void draw() final;
};

class InvisibleResizingButton : public Widget {
  public:
    InvisibleResizingButton(bool vertical, bool reverse_drag_dir, WidgetPos pos, WidgetPos* output_pos);
    void draw() final;

    inline static InvisibleResizingButton* resizing_instance{};
    bool is_vertical;
    bool is_drag_dir_reversed;
    WidgetPos* output_pos;
};

class MeshInspectorWidget : public Widget {
  public:
    void draw() final;

    Handle<Scene::ModelInstance> inspected;
};

class UI {
  public:
    UI();
    void update();

    InstancedModelListWidget imlist_widget{};
    RenderOutputWidget render_output_widget{};
    EngineLogWidget engine_log_widget{};
    InvisibleResizingButton imlist_resizing_btn{ true, false, {}, &imlist_widget.pos };
    InvisibleResizingButton engine_log_resizing_btn{ false, false, {}, &engine_log_widget.pos };
    MeshInspectorWidget mesh_inspector_widget;
    InvisibleResizingButton mesh_inspector_resizing_btn{ true, true, {}, &mesh_inspector_widget.pos };
};