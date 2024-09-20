#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>
#include "ui.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"

struct WidgetPos {
    int x{}, y{};
    int w{}, h{};
};

class InstancedModelListWidget {
  public:
    void draw() {
        auto& renderer = get_renderer();
        ImGui::PushItemWidth(pos.w);
        for(const auto& mi : renderer.model_instances) {
            ImGui::PushID(&mi);
            if(ImGui::TreeNode(mi.model->metadata->name.c_str())) {
                for(auto i = mi.model->first_mesh; i < mi.model->first_mesh + mi.model->mesh_count; ++i) {
                    const auto& msi = renderer.meshes.at(i);
                    ImGui::PushID(&msi);
                    if(ImGui::TreeNode(msi.metadata->name.c_str())) { ImGui::TreePop(); }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::PopItemWidth();
    }

    WidgetPos pos;
};

class RenderOutputWidget {
  public:
    void draw() {
        auto& renderer = get_renderer();
        renderer.set_screen_rect({
            .offset_x = pos.x,
            .offset_y = pos.y,
            .width = (uint32_t)pos.w,
            .height = (uint32_t)pos.h,
        });
    }

    WidgetPos pos;
};

class EngineLogWidget {
  public:
    void draw() {
        for(const auto& e : Engine::get()->msg_log) {
            ImGui::Text(e.c_str());
        }
    }

    WidgetPos pos;
};

class InvisibleResizingButton {
  public:
    InvisibleResizingButton(bool vertical, WidgetPos pos, WidgetPos* output_pos)
        : is_vertical{ vertical }, pos{ pos }, output_pos{ output_pos } {}
    void draw() {
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
                output_pos->w += ImGui::GetMouseDragDelta(0, 0.0f).x;
            } else {
                output_pos->h -= ImGui::GetMouseDragDelta(0, 0.0f).y;
            }
            ImGui::ResetMouseDragDelta();
        } else if(resizing_instance == this) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            resizing_instance = nullptr;
        }
        ImGui::PopClipRect();
    }

    inline static InvisibleResizingButton* resizing_instance{};
    bool is_vertical;
    WidgetPos pos;
    WidgetPos* output_pos;
};

static InstancedModelListWidget imlist_widget{};
static RenderOutputWidget render_output_widget{};
static EngineLogWidget engine_log_widget{};
static InvisibleResizingButton imlist_resizing_btn{ true, {}, &imlist_widget.pos };
static InvisibleResizingButton engine_log_resizing_btn{ false, {}, &engine_log_widget.pos };

UI::UI() {
    imlist_widget.pos.w = 200;
    engine_log_widget.pos.h = 250;
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

    if(ImGui::BeginTable("table", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV,
                         { -1.0f, ImGui::GetContentRegionMax().y - engine_log_widget.pos.h })) {
        ImGui::TableSetupColumn("l1", ImGuiTableColumnFlags_WidthFixed, imlist_widget.pos.w);
        ImGui::TableSetupColumn("l2", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        {
            const auto cpos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({ cpos.x + imlist_widget.pos.w, cpos.y });
            imlist_resizing_btn.pos.w = ImGui::GetStyle().ItemSpacing.y * 2.0f;
            imlist_resizing_btn.pos.h = ImGui::GetContentRegionMax().y - engine_log_widget.pos.h;
            imlist_resizing_btn.draw();
            ImGui::SetCursorScreenPos(cpos);
        }

        { imlist_widget.draw(); }

        ImGui::TableSetColumnIndex(1);
        {
            WidgetPos output_pos{ ImGui::GetCursorScreenPos().x,
                                  ImGui::GetCursorScreenPos().y + ImGui::GetStyle().ItemSpacing.y,
                                  ImGui::GetContentRegionAvail().x,
                                  ImGui::GetContentRegionAvail().y - engine_log_widget.pos.h };

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