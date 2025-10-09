#include <array>
#include <new>
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <eng/engine.hpp>
#include <eng/ui.hpp>
#include <eng/scene.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>

namespace eng
{

void UI::init()
{
    use_default_layout = !std::filesystem::exists("imgui.ini");

    eng::Engine::get().renderer->imgui_renderer->ui_callbacks += ([this] {
        static bool once = true;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, 0u);
        viewport_imid = ImGui::DockSpaceOverViewport(0, 0, ImGuiDockNodeFlags_PassthruCentralNode);
        if(once && use_default_layout)
        {
            left_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Left, 0.2f, nullptr, &viewport_imid);
            right_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Right, 0.25f, nullptr, &viewport_imid);
            bottom_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Down, 0.3f, nullptr, &viewport_imid);
            // viewport_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Down, 0.0f, nullptr, &viewport_imid);

            // ImGui::DockBuilderRemoveNode(viewport_imid);
            // ImGui::DockBuilderAddNode(viewport_imid);
            // ImGui::DockBuilderSetNodeSize(viewport_imid, ImGui::GetMainViewport()->Size);

            for(const auto& e : tabs)
            {
                switch(e.location)
                {
                case Location::LEFT_PANE:
                {
                    ImGui::DockBuilderDockWindow(e.name.c_str(), left_imid);
                    break;
                }
                case Location::RIGHT_PANE:
                {
                    ImGui::DockBuilderDockWindow(e.name.c_str(), right_imid);
                    break;
                }
                case Location::BOTTOM_PANE:
                {
                    ImGui::DockBuilderDockWindow(e.name.c_str(), bottom_imid);
                    break;
                }
                case Location::CENTER_PANE:
                {
                    ImGui::DockBuilderDockWindow(e.name.c_str(), viewport_imid);
                    break;
                }
                // case Location::NEW_PANE:
                //{
                //     break;
                // }
                default:
                {
                    ENG_ERROR("Unrecognized case");
                    break;
                }
                }
            }
            ImGui::DockBuilderFinish(viewport_imid);
            use_default_layout = false;
        }

        ImGui::PopStyleColor();
        for(const auto& e : tabs)
        {
            e.cb_func();
        }
        once = false;
    });
}

void UI::update() {}

} // namespace eng