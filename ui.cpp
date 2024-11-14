#include "engine.hpp"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <array>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ui.hpp"
#include "renderer_vulkan.hpp"

static std::array<std::pair<VkImageView, VkDescriptorSet>, 2> output_images;

void UI::update() {
    auto renderer = ((RendererVulkan*)Engine::renderer());
    if(Engine::window()->height == 0) { return; }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGui::SetNextWindowPos({});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if(ImGui::Begin("asdf", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        ImGuiCur c1;
        node_list.draw();
        auto rs1 = ImGui::GetItemRectSize();
        console.draw();
        ImGuiCur{ c1.x + rs1.x + ImGui::GetStyle().FramePadding.x, c1.y }.set_pos();
        ImGui::PopStyleColor();
        routput.draw();
    }
    ImGui::End();
    ImGui::Render();
}

ImGuiCur::ImGuiCur() { get_screen_pos(); }

ImGuiCur::ImGuiCur(float x, float y) : x(x), y(y) {}

void ImGuiCur::get_pos() { std::tie(x, y) = std::tuple{ ImGui::GetCursorPos().x, ImGui::GetCursorPos().y }; }

void ImGuiCur::get_screen_pos() {
    std::tie(x, y) = std::tuple{ ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y };
}

void ImGuiCur::set_pos() { ImGui::SetCursorPos({ x, y }); }

void ImGuiCur::offset(float x, float y) {
    this->x += x;
    this->y += y;
}

void UIWindow::get_window_pos() { std::tie(x, y) = std::tuple{ ImGui::GetWindowPos().x, ImGui::GetWindowPos().y }; }

void UIWindow::get_window_size() { std::tie(w, h) = std::tuple{ ImGui::GetWindowSize().x, ImGui::GetWindowSize().y }; }

void UIWindow::set_window_pos() { ImGui::SetNextWindowPos({ x, y }); }

void UIWindow::set_window_size() { ImGui::SetNextWindowSize({ w, h }); }

void NodeList::draw() {
    if(Engine::frame_num() == 0) {
        window.w = 200.0f;
        window.h = ImGui::GetIO().DisplaySize.y * 0.7f;
    }
    if(ImGui::BeginChild("Scene", { window.w, window.h }, ImGuiChildFlags_Border)) {
        for(auto& n : Engine::scene()->nodes) {
            ImGui::PushID(&n);
            if(ImGui::Selectable(n.name.c_str(), selected_node == &n)) { selected_node = &n; }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    window.w = ImGui::GetItemRectSize().x;
    window.h = ImGui::GetItemRectSize().y;
}

void Console::draw() {
    window.w = ImGui::GetContentRegionAvail().x;
    window.h = ImGui::GetContentRegionAvail().y;
    if(ImGui::BeginChild("Console", { window.w, window.h }, ImGuiChildFlags_Border)) {
        ImGui::Text("console");
        ImGui::EndChild();
    }
}

void RenderOutput::draw() {
    ImGuiCur pos;
    window.x = pos.x;
    window.y = pos.y;
    window.w = ImGui::GetContentRegionAvail().x;
    window.h = Engine::ui()->node_list.window.h;
    if(ImGui::BeginChild("Render Output", { window.w, window.h }, ImGuiChildFlags_Border)) {
        if(Engine::ui()->node_list.selected_node) {
            ImGuizmo::SetRect(window.x, window.y, window.w, window.h);
            ImGuizmo::SetDrawlist();
            auto& io = ImGui::GetIO();
            const auto viewmat = Engine::camera()->get_view();
            const auto projmat = Engine::camera()->get_projection();
            ImGuizmo::OPERATION op = ImGuizmo::OPERATION::TRANSLATE;
            ImGuizmo::MODE mode = ImGuizmo::MODE::LOCAL;

            if(Engine::ui()->node_list.selected_node->has_component<cmps::RenderMesh>()) {
                cmps::RenderMesh& rm = Engine::ec()->get<cmps::RenderMesh>(Engine::ui()->node_list.selected_node->handle);
                glm::mat4 tt = glm::translate(Engine::scene()->final_transforms.at(Engine::scene()->entity_node_idxs.at(
                                                  Engine::ui()->node_list.selected_node->handle)),
                                              rm.mesh->aabb.center());
                glm::mat4 delta{};
                ImGuizmo::Manipulate(&viewmat[0][0], &projmat[0][0], op, mode, &tt[0][0], &delta[0][0]);
                glm::vec3 t, r, s;
                ImGuizmo::DecomposeMatrixToComponents(&delta[0][0], &t.x, &r.x, &s.x);
                if(ImGuizmo::IsUsing()) {
                    glm::mat4& cmps_transform =
                        Engine::ec()->get<cmps::Transform>(Engine::ui()->node_list.selected_node->handle).transform;
                    cmps_transform = glm::translate(cmps_transform, t);
                    Engine::scene()->update_transform(Engine::ui()->node_list.selected_node->handle);
                }
            }
        }
        ImGui::EndChild();
    }
}
