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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
    ImGui::SetNextWindowPos({});
    ImGui::SetNextWindowSize({ (float)Engine::window()->width, (float)Engine::window()->height });
    ImGui::SetNextWindowScroll({});
    ImGui::Begin("main ui window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar();

    if(ImGui::BeginMenuBar()) {
        ImGui::Button("asd");
        ImGui::EndMenuBar();
    }

    if(ImGui::BeginTable("table", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                         ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("l1", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("l2", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("l3", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        { draw_left_column(); }

        ImGui::TableSetColumnIndex(1);
        {
            const ImVec2 cell_padding = ImVec2{ ImGui::GetStyle().CellPadding.x + ImGui::GetStyle().ChildBorderSize,
                                                ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().ChildBorderSize };
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - cell_padding.x - 1.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - cell_padding.y);
            auto cpos = ImGui::GetCursorScreenPos();
            const ImVec2 image_output_size{ ImGui::GetContentRegionAvail().x + cell_padding.x,
                                            ImGui::GetContentRegionAvail().y * 0.5f };

            if(ImGui::BeginChild("image output", image_output_size, ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY)) {
                const auto fn = get_renderer().get_resource_idx();
                const Image* oi = &get_renderer().output_images[fn];
                if(oi && oi->view && output_images[fn].first != oi->view) {
                    const auto linear_sampler = get_renderer().samplers.get_sampler();
                    if(output_images[fn].second) {
                        vkDeviceWaitIdle(get_renderer().dev);
                        ImGui_ImplVulkan_RemoveTexture(output_images[fn].second);
                    }
                    output_images[fn].first = oi->view;
                    output_images[fn].second =
                        ImGui_ImplVulkan_AddTexture(linear_sampler, oi->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                if(oi && oi->view) {
                    ImGui::Image((ImTextureID)output_images[fn].second, ImGui::GetContentRegionAvail());
                }

                if(draw_scene_selected) {
                    ScreenRect srect{ .offset_x = (int)output_offset_x,
                                      .offset_y = (int)output_offset_y,
                                      .width = (uint32_t)output_offset_w,
                                      .height = (uint32_t)output_offset_h };
                    ImGuizmo::SetRect(srect.offset_x, srect.offset_y, srect.width, srect.height);
                    ImGuizmo::SetDrawlist();
                    auto& io = ImGui::GetIO();
                    const auto viewmat = Engine::camera()->get_view();
                    const auto projmat = Engine::camera()->get_projection();
                    ImGuizmo::OPERATION op = ImGuizmo::OPERATION::TRANSLATE;
                    ImGuizmo::MODE mode = ImGuizmo::MODE::LOCAL;

                    // TODO: MAKE BOUNDING BOX UNRELATED TO RENDER MESH
                    if(draw_scene_selected->has_component<cmps::RenderMesh>()) {
                        cmps::RenderMesh& rm = Engine::ec()->get<cmps::RenderMesh>(draw_scene_selected->handle);
                        glm::mat4 tt = glm::translate(Engine::scene()->final_transforms.at(
                                                          Engine::scene()->entity_node_idxs.at(draw_scene_selected->handle)),
                                                      rm.mesh->aabb.center());
                        glm::mat4 delta;
                        ImGuizmo::Manipulate(&viewmat[0][0], &projmat[0][0], op, mode, &tt[0][0], &delta[0][0]);
                        glm::vec3 t, r, s;
                        ImGuizmo::DecomposeMatrixToComponents(&delta[0][0], &t.x, &r.x, &s.x);
                        if(ImGuizmo::IsUsing()) {
                            glm::mat4& cmps_transform = Engine::ec()->get<cmps::Transform>(draw_scene_selected->handle).transform;
                            cmps_transform = glm::translate(cmps_transform, t);
                            Engine::scene()->update_transform(draw_scene_selected->handle);
                            Engine::renderer()->update_transform(rm.render_handle);
                        }
                    }
                }
            }
            ImGui::EndChild();

            static constexpr float aspect = 16.0f / 9.0f;
            const float orig_width = image_output_size.x - ImGui::GetStyle().ItemSpacing.x * 0.5f + 1.0f;
            const float orig_height = ImGui::GetCursorScreenPos().y - cpos.y - ImGui::GetStyle().ItemSpacing.y - 5.0f;
            float width = orig_width;
            float height = orig_height;
            if(width > height * aspect) {
                width = height * aspect;
            } else {
                height = width / aspect;
            }
            float offsetX = cpos.x + 2.0f + std::abs(orig_width - width) * 0.5f;
            float offsetY = cpos.y + ImGui::GetStyle().ItemSpacing.y + std::abs(orig_height - height) * 0.5f;
            output_offset_x = offsetX;
            output_offset_y = offsetY;
            output_offset_w = width;
            output_offset_h = height;
            Engine::renderer()->set_screen_rect({ (int)offsetX, (int)offsetY, (uint32_t)width, (uint32_t)height });

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - cell_padding.x);
            if(ImGui::BeginChild("log output", ImVec2{ ImGui::GetContentRegionAvail().x + cell_padding.x,
                                                       ImGui::GetContentRegionAvail().y + cell_padding.y })) {
                draw_bottom_shelf();
            }
            ImGui::EndChild();
        }

        ImGui::TableSetColumnIndex(2);
        { draw_right_column(); }
        ImGui::EndTable();
    }
    ImGui::End();
    ImGui::Render();
}

void UI::draw_left_column() {
    auto* scene = Engine::scene();
    if(ImGui::BeginTabBar("draw_scene_hierarchy_tab_bar")) {
        if(ImGui::BeginTabItem("Scene", nullptr, ImGuiTabItemFlags_SetSelected)) { ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::BeginChild("scene hierarchy");
    for(auto rn : scene->root_nodes) {
        Node& node = scene->nodes.at(rn);
        ImGui::PushID(&node);
        bool& expanded = draw_scene_expanded[&node];
        bool selected = &node == draw_scene_selected;
        auto cposx = ImGui::GetCursorPosX();
        if(ImGui::CollapsingHeader(node.name.c_str())) {
            ImGui::Indent();
            // TODO: traverse the children
            for(auto& n : std::span{ scene->nodes.data() + rn + node.children_offset, node.children_count }) {
                ImGui::PushID(&n);
                if(ImGui::Selectable(n.name.c_str(), &n == draw_scene_selected)) { set_selected(&n); }
                ImGui::PopID();
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void UI::draw_bottom_shelf() {
    int mode = -1;
    if(ImGui::BeginTabBar("bottom tab bar")) {
        if(ImGui::BeginTabItem("Debug", nullptr, ImGuiTabItemFlags_SetSelected)) {
            mode = 0;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    switch(mode) {
    case 0: {
        if(ImGui::BeginChild("output log window", {}, {}, ImGuiWindowFlags_HorizontalScrollbar)) {
            float alternating = 0.0f;
            for(const auto& msg : Engine::get()->msg_log) {
                alternating = alternating + (1.0f - alternating) - 1.0f * alternating;
                auto col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                col.w *= (1.0f - alternating) * 0.75f + alternating * 0.6f;
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::Text(msg.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
    } break;
    default: {
        return;
    }
    }
}

void UI::draw_right_column() {
    Node* node = draw_scene_selected;
    if(!node) { return; }
    glm::mat4& t = Engine::ec()->get<cmps::Transform>(node->handle).transform;
    ImGui::PushID(node);
    ImGui::Text(node->name.c_str());
    if(ImGui::SliderFloat3("##transform", &t[3].x, -1.0f, 1.0f)) {
        if(node->has_component<cmps::RenderMesh>()) {
            cmps::RenderMesh& rm = Engine::ec()->get<cmps::RenderMesh>(node->handle);
            Engine::scene()->update_transform(node->handle);
            Engine::renderer()->update_transform(rm.render_handle);
        }
    }
    ImGui::PopID();
}

void UI::set_selected(Node* ptr) { draw_scene_selected = ptr; }
