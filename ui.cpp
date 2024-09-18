#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>
#include "ui.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"

static VkDescriptorSet output_image_set[2]{};

void UI::update() {
    if(Engine::frame_num() < 6) { return; }

    auto renderer = ((RendererVulkan*)Engine::renderer());

    if(!output_image_set[0]) {
        auto sampler = renderer->samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        for(int i = 0; i < 2; ++i) {
            if(!renderer->output_images[Engine::frame_num()].view) { return; }
            output_image_set[i] =
                ImGui_ImplVulkan_AddTexture(sampler, renderer->output_images[i].view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
        }
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("##a", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    if(ImGui::BeginTable("table", 2)) {
        ImGui::TableSetupColumn("l1", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("l2");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        {
            for(int i = 0; i < 10; ++i) {
                ImGui::PushID(i);
                if(ImGui::TreeNode("gugu")) {
                    ImGui::Text("asdf");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        ImGui::TableSetColumnIndex(1);
        {
            static constexpr auto ASPECT = 768.0 / 1280.0;
            renderer->set_screen_rect({ .offset_x = 150,
                                        .offset_y = 0,
                                        .width = (uint32_t)ImGui::GetContentRegionAvail().x,
                                        .height = (uint32_t)(ImGui::GetContentRegionAvail().x * ASPECT) });
        }
    }
    ImGui::EndTable();

    /*ImGui::SetNextWindowPos({});
    ImGui::SetNextWindowSize({ (float)Engine::window()->width, (float)Engine::window()->height });
    auto cpos = ImGui::GetCursorPos();

    if(ImGui::BeginChild("project view", { 150, ImGui::GetContentRegionAvail().y })) { ImGui::Text("asdf"); }
    ImGui::EndChild();

    ImGui::SetNextWindowPos({ cpos.x + 150, cpos.y });

    if(ImGui::BeginChild("project view 1", { 150, ImGui::GetContentRegionAvail().y })) { ImGui::Text("asdf"); }
    ImGui::EndChild();*/

    /*

    ImGui::Image(output_image_set[Engine::frame_num() % 2], { 500, 500 });*/

    ImGui::End();
    ImGui::Render();
}