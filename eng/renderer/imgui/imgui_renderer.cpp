#include "imgui_renderer.hpp"
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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

    // cmdpool = r->submit_queue->make_command_pool();

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

    vertex_buffer =
        r->make_buffer(BufferCreateInfo{ "imgui vertex buffer", 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false });
    index_buffer = r->make_buffer(BufferCreateInfo{ "imgui index buffer", 1024 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, false });
}

void ImGuiRenderer::render(CommandBuffer* cmd)
{
    auto* r = RendererVulkan::get_instance();
    // auto* cmd = cmdpool->begin();
    //  ImGui::SetCurrentContext(Engine::get().ui_ctx->imgui_ctx);
    // ImGui_ImplVulkan_Init
    // ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    // ImGuizmo::BeginFrame();

    // Engine::get().ui->update();
    ImGui::SetNextWindowPos({}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({ 200.0f, Engine::get().window->height }, ImGuiCond_Always);
    ImGui::Begin("test", 0, ImGuiWindowFlags_NoMove);
    static float f = 0.0f;
    ImGui::SliderFloat("lalala", &f, 0.0f, 1.0f);
    ImGui::End();
    ImGui::Begin("test2");
    ImGui::Text("asdf");
    ImGui::End();
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if(fb_width <= 0 || fb_height <= 0) { return; }
    if(draw_data)
    {
        uint64_t vtx_off = 0;
        uint64_t idx_off = 0;
        int global_vtx_offset = 0;
        int global_idx_offset = 0;
        auto* draw_data = ImGui::GetDrawData();
        auto* smgr = r->staging_manager;
        for(int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* draw_list = draw_data->CmdLists[n];
            smgr->copy(vertex_buffer, draw_list->VtxBuffer.Data, vtx_off, { 0ull, draw_list->VtxBuffer.Size * sizeof(ImDrawVert) });
            smgr->copy(index_buffer, draw_list->IdxBuffer.Data, idx_off, { 0ull, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx) });
            vtx_off += draw_list->VtxBuffer.Size * sizeof(ImDrawVert);
            idx_off += draw_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        }
        smgr->flush()->wait_cpu(~0ull);

        VkRenderingAttachmentInfo r_col_atts[]{
            Vks(VkRenderingAttachmentInfo{
                .imageView = r->swapchain.get_current_view(),
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
            }),
        };
        auto rendering_info = Vks(VkRenderingInfo{
            .renderArea = { .extent = { .width = (uint32_t)fb_width, .height = (uint32_t)fb_height } },
            .layerCount = 1,
            .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
            .pColorAttachments = r_col_atts,
        });

        cmd->bind_index(index_buffer.get(), 0, VK_INDEX_TYPE_UINT16);
        cmd->bind_pipeline(pipeline.get());
        r->bindless_pool->bind(cmd);
        {
            float scale[2];
            scale[0] = 2.0f / draw_data->DisplaySize.x;
            scale[1] = 2.0f / draw_data->DisplaySize.y;
            float translate[2];
            translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
            translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
            cmd->push_constants(VK_SHADER_STAGE_ALL, scale, { 0, 8 });
            cmd->push_constants(VK_SHADER_STAGE_ALL, translate, { 8, 8 });
        }

        {
            const auto vertex_idx = r->bindless_pool->get_index(vertex_buffer);
            const auto texture_idx = r->bindless_pool->get_index(font_texture);
            cmd->push_constants(VK_SHADER_STAGE_ALL, &vertex_idx, { 16, 4 });
            cmd->push_constants(VK_SHADER_STAGE_ALL, &texture_idx, { 20, 4 });
        }

        cmd->begin_rendering(rendering_info);
        {
            VkViewport viewport;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = (float)fb_width;
            viewport.height = (float)fb_height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            cmd->set_viewports(&viewport, 1);
        }

        ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
        for(int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* draw_list = draw_data->CmdLists[n];
            for(int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];

                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                if(clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if(clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if(clip_max.x > fb_width) { clip_max.x = (float)fb_width; }
                if(clip_max.y > fb_height) { clip_max.y = (float)fb_height; }
                if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) { continue; }

                // Apply scissor/clipping rectangle
                VkRect2D scissor;
                scissor.offset.x = (int32_t)(clip_min.x);
                scissor.offset.y = (int32_t)(clip_min.y);
                scissor.extent.width = (uint32_t)(clip_max.x - clip_min.x);
                scissor.extent.height = (uint32_t)(clip_max.y - clip_min.y);
                cmd->set_scissors(&scissor, 1);

                // Bind DescriptorSet with font or user texture
                // VkDescriptorSet desc_set[1] = { (VkDescriptorSet)pcmd->TextureId };
                // if(sizeof(ImTextureID) < sizeof(ImU64))
                //{
                //     // We don't support texture switches if ImTextureID hasn't been redefined to be 64-bit. Do a
                //     flaky check that other textures haven't been used. IM_ASSERT(pcmd->TextureId ==
                //     (ImTextureID)bd->FontDescriptorSet); desc_set[0] = bd->FontDescriptorSet;
                // }

                cmd->draw_indexed(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
            }
            global_idx_offset += draw_list->IdxBuffer.Size;
            global_vtx_offset += draw_list->VtxBuffer.Size;
        }
        cmd->end_rendering();
    }
}
} // namespace gfx
