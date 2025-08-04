#include "imgui_renderer.hpp"
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/engine.hpp>
#include <third_party/imgui/imgui.h>
#include <third_party/imgui/backends/imgui_impl_glfw.h>
#include <third_party/imgui/backends/imgui_impl_vulkan.h>

namespace gfx
{
void ImGuiRenderer::initialize()
{
    auto* r = RendererVulkan::get_instance();

    IMGUI_CHECKVERSION();
    void* user_data;
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(Engine::get().window->window, true);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    size_t upload_size = width * height * 4 * sizeof(char);

    pipeline = r->make_pipeline(PipelineCreateInfo{
        .shaders = { r->make_shader(ShaderStage::VERTEX, "imgui/imgui.vert.glsl"), r->make_shader(ShaderStage::PIXEL, "imgui/imgui.frag.glsl") },
        .attachments = { .count = 1,
                         .color_formats = { ImageFormat::R8G8B8A8_SRGB },
                         .blend_states = { PipelineCreateInfo::BlendState{
                             .enable = true,
                             .src_color_factor = BlendFactor::SRC_ALPHA,
                             .dst_color_factor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                             .color_op = BlendOp::ADD,
                             .src_alpha_factor = BlendFactor::ONE,
                             .dst_alpha_factor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                             .alpha_op = BlendOp::ADD,
                         } } },
        .culling = CullFace::NONE,
    });

    sampler = r->batch_sampler(gfx::SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE } });

    cmdpool = r->submit_queue->make_command_pool();

    font_image = r->batch_image(ImageDescriptor{
        .name = "imgui font",
        .width = (uint32_t)width,
        .height = (uint32_t)height,
        .depth = 1,
        .format = ImageFormat::R8G8B8A8_UNORM,
        .type = ImageType::TYPE_2D,
        .data = std::as_bytes(std::span{ pixels, upload_size }),
    });

    font_texture = r->batch_texture(TextureDescriptor{ font_image, sampler });
}

void ImGuiRenderer::render()
{
    //auto& r = *RendererVulkan::get_instance();
    //auto* cmd = cmdpool->begin();
    //// ImGui::SetCurrentContext(Engine::get().ui_ctx->imgui_ctx);
    //ImGui_ImplVulkan_NewFrame();
    //ImGui_ImplGlfw_NewFrame();
    //ImGui::NewFrame();
    //// ImGuizmo::BeginFrame();

    //// Engine::get().ui->update();
    //ImGui::Begin("test");
    //static float f = 0.0f;
    //ImGui::SliderFloat("lalala", &f, 0.0f, 1.0f);
    //ImGui::End();

    //ImGui::Render();
    //ImDrawData* im_draw_data = ImGui::GetDrawData();
    //if(im_draw_data)
    //{
    //    VkRenderingAttachmentInfo r_col_atts[]{
    //        Vks(VkRenderingAttachmentInfo{
    //            .imageView = r.swapchain.get_current_view(),
    //            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    //            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    //            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    //            .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
    //        }),
    //    };
    //    VkRect2D r_sciss_1{ .offset = {},
    //                        .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
    //    VkViewport r_view_1{ .x = 0.0f,
    //                         .y = Engine::get().window->height,
    //                         .width = Engine::get().window->width,
    //                         .height = Engine::get().window->height,
    //                         .minDepth = 0.0f,
    //                         .maxDepth = 1.0f };
    //    auto rendering_info = Vks(VkRenderingInfo{
    //        .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
    //                                    .height = (uint32_t)Engine::get().window->height } },
    //        .layerCount = 1,
    //        .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
    //        .pColorAttachments = r_col_atts,
    //    });
    //    vkCmdBeginRendering(cmd, &rendering_info);
    //    vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
    //    vkCmdSetViewport(cmd, 0, 1, &r_view_1);
    //    ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
    //    vkCmdEndRendering(cmd);
    //}
}
} // namespace gfx
