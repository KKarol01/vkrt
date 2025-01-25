#include "engine.hpp"
#include <array>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ui.hpp"
#include "renderer_vulkan.hpp"

static ImTextureID get_imgui_render_output_descriptor();

void UI::update() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGui::SetNextWindowPos({});
    ImGui::SetNextWindowSize(ImVec2{ Engine::get().window->width, Engine::get().window->height });
    if(ImGui::Begin("main ui panel", 0,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar)) {
        if(ImGui::BeginMenuBar()) {
            ImGui::MenuItem("LOLOLOL", "CTRL-S");
            ImGui::EndMenuBar();
        }
        const auto render_output_size = ImGui::GetContentRegionMax() * ImVec2{ 0.8f, 0.7f };
        // used primarily for render output
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
        if(ImGui::BeginChild("render output panel", render_output_size, ImGuiChildFlags_Border)) {
            ImGui::Image(get_imgui_render_output_descriptor(), render_output_size);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        const auto render_output_next_row = ImGui::GetCursorScreenPos();
        ImGui::SameLine();
        // used for scene manipulation
        if(ImGui::BeginChild("scene panel", {}, ImGuiChildFlags_Border)) {}
        ImGui::EndChild();

        // used primarily for debug output
        ImGui::SetCursorScreenPos(render_output_next_row);
        if(ImGui::BeginChild("engine panel", { render_output_size.x, 0.0f }, ImGuiChildFlags_Border)) {}
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Render();
}

ImTextureID get_imgui_render_output_descriptor() {
    static std::unordered_map<VkImageView, VkDescriptorSet> render_output_view_sets;
    static VkSampler sampler = get_renderer().samplers.get_sampler();
    const auto view = get_renderer().get_image(get_renderer().get_frame_data().gbuffer.color_image).view;
    if(auto it = render_output_view_sets.find(view); it != render_output_view_sets.end()) {
        return reinterpret_cast<ImTextureID>(it->second);
    }
    if(render_output_view_sets.size() > 512) {
        for(auto& e : render_output_view_sets) {
            ImGui_ImplVulkan_RemoveTexture(e.second);
        }
        render_output_view_sets.clear();
    }
    return reinterpret_cast<ImTextureID>(render_output_view_sets
                                             .emplace(view, ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL))
                                             .first->second);
    return ImTextureID{};
}
