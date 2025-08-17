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
        .shaders = { r->make_shader(ShaderStage::VERTEX_BIT, "imgui/imgui.vert.glsl"),
                     r->make_shader(ShaderStage::PIXEL_BIT, "imgui/imgui.frag.glsl") },
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

    sampler = r->make_sampler(gfx::SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE } });

    // cmdpool = r->submit_queue->make_command_pool();

    // font_image = r->batch_image(ImageDescriptor{
    //     .name = "imgui font",
    //     .width = (uint32_t)width,
    //     .height = (uint32_t)height,
    //     .depth = 1,
    //     .format = ImageFormat::R8G8B8A8_UNORM,
    //     .type = ImageType::TYPE_2D,
    //     .data = std::as_bytes(std::span{ pixels, upload_size }),
    // });

    // font_texture = r->batch_texture(TextureDescriptor{ font_image, sampler });

    vertex_buffer = r->make_buffer(BufferDescriptor{ "imgui vertex buffer", 1024 * 1024, BufferUsage::STORAGE_BIT });
    index_buffer = r->make_buffer(BufferDescriptor{ "imgui index buffer", 1024 * 1024, BufferUsage::INDEX_BIT });
}

void ImGuiRenderer::render(CommandBuffer* cmd)
{
    auto* r = RendererVulkan::get_instance();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    for(auto& e : ui_callbacks)
    {
        if(e) { e(); }
    }
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();
    if(!draw_data) { return; }
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if(fb_width <= 0 || fb_height <= 0) { return; }
    if(draw_data->Textures != nullptr)
    {
        for(ImTextureData* tex : *draw_data->Textures)
        {
            handle_imtexture(tex);
        }
    }

    uint64_t vtx_off = 0;
    uint64_t idx_off = 0;
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    auto* smgr = r->staging_manager;
    for(int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        smgr->copy(vertex_buffer, draw_list->VtxBuffer.Data, vtx_off, { 0ull, draw_list->VtxBuffer.Size * sizeof(ImDrawVert) });
        smgr->copy(index_buffer, draw_list->IdxBuffer.Data, idx_off, { 0ull, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx) });
        vtx_off += draw_list->VtxBuffer.Size * sizeof(ImDrawVert);
        idx_off += draw_list->IdxBuffer.Size * sizeof(ImDrawIdx);
    }

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
        cmd->push_constants(VK_SHADER_STAGE_ALL, &vertex_idx, { 16, 4 });
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
            const ImDrawCmd* imcmd = &draw_list->CmdBuffer[cmd_i];

            // Project scissor/clipping rectangles into framebuffer space
            ImVec2 clip_min((imcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                            (imcmd->ClipRect.y - clip_off.y) * clip_scale.y);
            ImVec2 clip_max((imcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                            (imcmd->ClipRect.w - clip_off.y) * clip_scale.y);

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

            const auto texid = r->bindless_pool->get_index(Handle<Texture>{ (uint32_t)imcmd->GetTexID() - 1 });
            cmd->push_constants(VK_SHADER_STAGE_ALL, &texid, { 20, 4 });
            cmd->draw_indexed(imcmd->ElemCount, 1, imcmd->IdxOffset + global_idx_offset, imcmd->VtxOffset + global_vtx_offset, 0);
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }
    cmd->end_rendering();
}

uint32_t ImGuiRenderer::add_ui_callback(const callback_t& cb)
{
    uint32_t idx;
    if(free_ui_callbacks.size())
    {
        idx = free_ui_callbacks.front();
        free_ui_callbacks.pop_front();
    }
    else
    {
        idx = ui_callbacks.size();
        ui_callbacks.push_back(cb);
        return idx;
    }
    ui_callbacks.at(idx) = cb;
    return idx;
}

void ImGuiRenderer::remove_ui_callback(uint32_t idx)
{
    assert(idx < ui_callbacks.size());
    if(ui_callbacks.at(idx))
    {
        free_ui_callbacks.push_back(idx);
        ui_callbacks.at(idx) = nullptr;
    }
}

void ImGuiRenderer::handle_imtexture(ImTextureData* imtex)
{
    if(imtex->Status == ImTextureStatus_OK) { return; }

    auto* r = RendererVulkan::get_instance();
    Handle<Image> image;
    if(imtex->Status == ImTextureStatus_WantCreate)
    {
        image = r->make_image(ImageDescriptor{ "imgui image", (uint32_t)imtex->Width, (uint32_t)imtex->Height, 1u, 1u,
                                               ImageFormat::R8G8B8A8_UNORM, ImageType::TYPE_2D,
                                               ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_DST_BIT });
        auto texture = r->make_texture(TextureDescriptor{ image->default_view, sampler });
        images.push_back(image);
        textures.push_back(texture);
        imtex->SetTexID((ImTextureID)(*texture + 1)); // +1 so GetTexID doesn't complain when it's 0.
    }

    if(imtex->Status == ImTextureStatus_WantCreate || imtex->Status == ImTextureStatus_WantUpdates)
    {
        const int upload_x = (imtex->Status == ImTextureStatus_WantCreate) ? 0 : imtex->UpdateRect.x;
        const int upload_y = (imtex->Status == ImTextureStatus_WantCreate) ? 0 : imtex->UpdateRect.y;
        const int upload_w = (imtex->Status == ImTextureStatus_WantCreate) ? imtex->Width : imtex->UpdateRect.w;
        const int upload_h = (imtex->Status == ImTextureStatus_WantCreate) ? imtex->Height : imtex->UpdateRect.h;
        assert(image);
        assert(upload_w == imtex->Width && upload_h == imtex->Height);
        r->staging_manager->copy(image, imtex->Pixels, ImageLayout::READ_ONLY);
        imtex->SetStatus(ImTextureStatus_OK);
    }
}

} // namespace gfx
