#include "imgui_renderer.hpp"
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/engine.hpp>
#include <third_party/imgui/imgui.h>
#include <third_party/imgui/backends/imgui_impl_glfw.h>
#include <third_party/imgui/backends/imgui_impl_vulkan.h>

namespace gfx
{
void ImGuiRenderer::initialize() 
{
    //IMGUI_CHECKVERSION();
    //void* user_data;
    //ImGui::CreateContext();
    //ImGui::StyleColorsDark();
    //ImGui_ImplGlfw_InitForVulkan(Engine::get().window->window, true);	

    //    ImGuiIO& io = ImGui::GetIO();
    //io.Fonts->AddFontDefault();

    //auto cmdimgui = get_frame_data().cmdpool->begin();
    //ImGui_ImplVulkan_CreateFontsTexture();
    //get_frame_data().cmdpool->end(cmdimgui);
    //submit_queue->with_cmd_buf(cmdimgui).submit_wait(~0ull);

}

void ImGuiRenderer::begin() {}

void ImGuiRenderer::render() {}
} // namespace gfx
