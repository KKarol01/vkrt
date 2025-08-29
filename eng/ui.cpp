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

    eng::Engine::get().imgui_renderer->add_ui_callback([this] {
        viewport_imid = ImGui::GetID("viewport_dockspace");
        ImGui::DockSpaceOverViewport(viewport_imid, 0, ImGuiDockNodeFlags_PassthruCentralNode);
        if(use_default_layout)
        {
            left_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Left, 0.2f, nullptr, &viewport_imid);
            right_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Right, 0.25f, nullptr, &viewport_imid);
            bottom_imid = ImGui::DockBuilderSplitNode(viewport_imid, ImGuiDir_Down, 0.3f, nullptr, &viewport_imid);

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
                default:
                {
                    break;
                }
                }
            }
            ImGui::DockBuilderFinish(viewport_imid);
            use_default_layout = false;
        }

        for(const auto& e : tabs)
        {
            e.cb_func();
        }
    });
}

void UI::update() {}

} // namespace eng