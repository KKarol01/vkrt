#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include "ui.hpp"
#include "renderer_vulkan.hpp"

UI::UI() {
    imlist_widget.pos.w = 200;
    engine_log_widget.pos.h = 250;
    mesh_inspector_widget.pos.w = 200;
}

void UI::update() {
    auto renderer = ((RendererVulkan*)Engine::renderer());

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({});
    ImGui::SetNextWindowSize({ (float)Engine::window()->width, (float)Engine::window()->height });
    ImGui::Begin("##a", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    const auto table_pos = ImGui::GetCursorScreenPos();
    const auto table_columns = 2 + (mesh_inspector_widget.inspected ? 1 : 0);
    const auto table_height = ImGui::GetContentRegionMax().y - engine_log_widget.pos.h;

    if(ImGui::BeginTable("table", table_columns, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV, { 0.0f, table_height })) {
        ImGui::TableSetupColumn("l1", ImGuiTableColumnFlags_WidthFixed, imlist_widget.pos.w);
        ImGui::TableSetupColumn("l2", ImGuiTableColumnFlags_WidthStretch);
        if(table_columns >= 3) {
            ImGui::TableSetupColumn("l3", ImGuiTableColumnFlags_WidthFixed, mesh_inspector_widget.pos.w);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        {
            const auto cpos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({ cpos.x + imlist_widget.pos.w, cpos.y });
            imlist_resizing_btn.pos.w = ImGui::GetStyle().ItemSpacing.y * 2.0f;
            imlist_resizing_btn.pos.h = ImGui::GetContentRegionMax().y - engine_log_widget.pos.h;
            imlist_resizing_btn.draw();
            ImGui::SetCursorScreenPos(cpos);

            imlist_widget.draw();
        }

        ImGui::TableSetColumnIndex(1);
        {
            WidgetPos output_pos{ (int)ImGui::GetCursorScreenPos().x,
                                  (int)(ImGui::GetCursorScreenPos().y + ImGui::GetStyle().ItemSpacing.y),
                                  (int)ImGui::GetContentRegionAvail().x,
                                  (int)ImGui::GetContentRegionAvail().y - engine_log_widget.pos.h };

            static constexpr float RATIO = 16.0f / 9.0f;

            if(output_pos.w / RATIO <= output_pos.h) {
                output_pos.h = output_pos.w / RATIO;
            } else {
                output_pos.w = output_pos.h * RATIO;
            }

            output_pos.x += (ImGui::GetContentRegionAvail().x - output_pos.w) * 0.5f;
            output_pos.y += (ImGui::GetContentRegionAvail().y - engine_log_widget.pos.h - output_pos.h) * 0.5f;
            render_output_widget.pos = output_pos;
            render_output_widget.draw();
        }
    }

    if(mesh_inspector_widget.inspected && table_columns >= 3) {
        ImGui::TableSetColumnIndex(2);
        {
            const auto cpos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({ cpos.x - ImGui::GetStyle().ItemSpacing.y * 2.0f, cpos.y });
            mesh_inspector_resizing_btn.pos.w = ImGui::GetStyle().ItemSpacing.y * 2.0f;
            mesh_inspector_resizing_btn.pos.h = table_height;
            mesh_inspector_resizing_btn.draw();
            ImGui::SetCursorScreenPos(cpos);
            mesh_inspector_widget.draw();
        }
    }

    ImGui::EndTable();

    {
        const auto cpos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({ cpos.x, cpos.y - ImGui::GetStyle().ItemSpacing.y });
        engine_log_resizing_btn.pos.w = ImGui::GetContentRegionAvail().x;
        engine_log_resizing_btn.pos.h = ImGui::GetStyle().ItemSpacing.y * 2.0f;
        engine_log_resizing_btn.draw();
        ImGui::SetCursorScreenPos(cpos);
    }

    {
        if(ImGui::BeginChild("Engine output log", { -1.0f, (float)engine_log_widget.pos.h - ImGui::GetTextLineHeight() },
                             ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar)) {
            engine_log_widget.draw();
        }
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::Render();
}

void InstancedModelListWidget::draw() {
    auto& renderer = get_renderer();
    for(const auto& mi : Engine::scene()->model_instances) {
        draw_selectable_expandable_label(mi);
    }
}

void InstancedModelListWidget::draw_selectable_expandable_label(const Scene::ModelInstance& mi) {
    const auto& asset = Engine::scene()->model_assets.at(mi.asset);
    ImGui::PushID(&mi);
    ImGui::SetCursorPos(ImGui::GetCursorPos() - ImGui::GetStyle().FramePadding);
    auto& is_expanded = this->is_expanded[mi.handle];
    if(ImGui::ArrowButton("expand_btn", is_expanded ? ImGuiDir_Down : ImGuiDir_Right)) { is_expanded = !is_expanded; }
    ImGui::SameLine();
    ImGui::SetCursorPos(ImGui::GetCursorPos() - ImGui::GetStyle().FramePadding);
    if(ImGui::Selectable("asdf", selected == &mi, {}, { 0.0f, ImGui::GetFrameHeight() - 2.0f /* match selectable's height */ })) {
        selected = &mi;
    }
    if(is_expanded) {
        ImGui::Indent();
        for(const auto& msi : asset.meshes) {
            ImGui::PushID(&msi);
            if(ImGui::Selectable(msi.name.c_str(), selected == &msi)) { selected = &msi; }
            ImGui::PopID();
        }
        ImGui::Unindent();
    }
    if(selected) { Engine::ui()->mesh_inspector_widget.inspected = mi.handle; }
    ImGui::PopID();
}

void RenderOutputWidget::draw() {
    auto& renderer = get_renderer();
    renderer.set_screen_rect({
        .offset_x = pos.x,
        .offset_y = pos.y,
        .width = (uint32_t)pos.w,
        .height = (uint32_t)pos.h,
    });
}

void EngineLogWidget::draw() {
    for(const auto& e : Engine::get()->msg_log) {
        ImGui::Text(e.c_str());
    }
}

InvisibleResizingButton::InvisibleResizingButton(bool vertical, bool reverse_drag_dir, WidgetPos pos, WidgetPos* output_pos)
    : Widget{ pos }, is_vertical{ vertical }, is_drag_dir_reversed{ reverse_drag_dir }, output_pos{ output_pos } {}

void InvisibleResizingButton::draw() {
    ImGui::PushClipRect({}, { (float)Engine::window()->width, (float)Engine::window()->height }, false);
    ImGui::InvisibleButton("IMList Resize EW", { (float)pos.w, (float)pos.h });
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped) && !resizing_instance) {
        if(is_vertical) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
    }
    if(ImGui::IsItemActive() && (!resizing_instance || resizing_instance == this)) {
        resizing_instance = this;
        if(is_vertical) {
            output_pos->w += ImGui::GetMouseDragDelta(0, 0.0f).x * (is_drag_dir_reversed ? -1.0f : 1.0f);
        } else {
            output_pos->h -= ImGui::GetMouseDragDelta(0, 0.0f).y * (is_drag_dir_reversed ? -1.0f : 1.0f);
        }
        ImGui::ResetMouseDragDelta();
    } else if(resizing_instance == this) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        resizing_instance = nullptr;
    }
    ImGui::PopClipRect();
}

void MeshInspectorWidget::draw() {}
