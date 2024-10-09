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
    }
    ImGui::EndTable();
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
    for(auto& mi : scene->model_instances) {
        ImGui::PushID(&mi);
        bool& expanded = draw_scene_expanded[&mi];
        bool selected = &mi == draw_scene_selected;
        auto cposx = ImGui::GetCursorPosX();
        if(ImGui::CollapsingHeader(mi.name.c_str())) {
            ImGui::Indent();
            for(auto& msi : std::span{ scene->mesh_instances.data() + mi.instance_offset, mi.instance_count }) {
                ImGui::PushID(&msi);
                if(ImGui::Selectable(msi.mesh->name.c_str(), &msi == draw_scene_selected)) { set_selected(&msi); }
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
    if(!draw_scene_selected) { return; }
    Scene::MeshInstance* data = static_cast<Scene::MeshInstance*>(draw_scene_selected);
    Scene::ModelInstance& msi = Engine::scene()->model_instances.at(data->model_instance);
    if(ImGui::CollapsingHeader("Parent transform")) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if(ImGui::SliderFloat3("##transform", &msi.transform[3].x, -1.0f, 1.0f)) {
            for(auto& e : std::span{ Engine::scene()->mesh_instances.data() + msi.instance_offset, msi.instance_count }) {
                Engine::renderer()->update_transform(e.renderer_handle);
            }
        }
    }
    ImGui::Separator();

    const auto data_idx = std::distance(Engine::scene()->mesh_instances.data(), data);
    auto& t = Engine::scene()->transforms.at(data_idx);
    ImGui::PushID(data);
    ImGui::Text(data->mesh->name.c_str());
    if(ImGui::SliderFloat3("##transform", &t[3].x, -1.0f, 1.0f)) {
        Engine::renderer()->update_transform(data->renderer_handle);
    }
    ImGui::PopID();

    ScreenRect srect{ .offset_x = (int)output_offset_x,
                      .offset_y = (int)output_offset_y,
                      .width = (uint32_t)output_offset_w,
                      .height = (uint32_t)output_offset_h };
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(srect.offset_x, srect.offset_y, srect.width, srect.height);
    const auto viewmat = Engine::camera()->get_view();
    const auto projmat = Engine::camera()->get_projection();
    static ImGuizmo::OPERATION op = ImGuizmo::OPERATION::TRANSLATE;
    static ImGuizmo::MODE mode = ImGuizmo::MODE::WORLD;
    static glm::mat4 tt = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ -0.194f, -0.280f - 0.054f, 0.362f - 0.054f});
    ImGuizmo::Manipulate(&viewmat[0][0], &projmat[0][0], op, mode, &tt[0][0]);
}

void UI::set_selected(Scene::MeshInstance* ptr) {
    draw_scene_selected = ptr;
    draw_scene_selected_type = MESH_INSTANCE;
}
