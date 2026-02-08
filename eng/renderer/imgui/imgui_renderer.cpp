#include "imgui_renderer.hpp"
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/engine.hpp>
#include <third_party/imgui/imgui.h>
#include <third_party/imgui/imgui_internal.h>
#include <third_party/imgui/backends/imgui_impl_glfw.h>
#include <third_party/imgui/backends/imgui_impl_vulkan.h>
#include <third_party/ImGuizmo/ImGuizmo.h>

namespace eng
{
namespace gfx
{

void ImGuiRenderer::init()
{
    auto& r = get_renderer();

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

    pipeline = r.make_pipeline(PipelineCreateInfo{
        .shaders = { r.make_shader("imgui/imgui.vert.glsl"), r.make_shader("imgui/imgui.frag.glsl") },
        .layout = r.common_playout,
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

    sampler = r.make_sampler(gfx::SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE } });

    vertex_buffer = r.make_buffer(Buffer::init("imgui vertex buffer", 1024 * 1024, BufferUsage::STORAGE_BIT));
    index_buffer = r.make_buffer(Buffer::init("imgui index buffer", 1024 * 1024, BufferUsage::INDEX_BIT));
}

void ImGuiRenderer::update(CommandBufferVk* cmd, ImageView output)
{
    // auto& r = get_renderer();
    // ImGui_ImplGlfw_NewFrame();
    // ImGui::NewFrame();
    // ImGuizmo::BeginFrame();

    // ui_callbacks.signal();

    // ImGui::Render();

    // ImDrawData* draw_data = ImGui::GetDrawData();
    // if(!draw_data) { return; }
    // int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    // int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    // if(fb_width <= 0 || fb_height <= 0) { return; }
    // if(draw_data->Textures != nullptr)
    //{
    //     for(ImTextureData* tex : *draw_data->Textures)
    //     {
    //         handle_imtexture(tex);
    //     }
    // }

    // uint64_t vtx_off = 0;
    // uint64_t idx_off = 0;
    // int global_vtx_offset = 0;
    // int global_idx_offset = 0;
    // auto* sbuf = r.sbuf;
    // for(int n = 0; n < draw_data->CmdListsCount; n++)
    //{
    //     const ImDrawList* draw_list = draw_data->CmdLists[n];
    //     r.sbuf->copy(vertex_buffer, draw_list->VtxBuffer.Data, vtx_off, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
    //     r.sbuf->copy(index_buffer, draw_list->IdxBuffer.Data, idx_off, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
    //     vtx_off += draw_list->VtxBuffer.Size * sizeof(ImDrawVert);
    //     idx_off += draw_list->IdxBuffer.Size * sizeof(ImDrawIdx);
    // }
    // auto* sbufs = r.make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "imguisbuf" });
    // sbuf->flush()->wait_cpu(~0ull);
    // r.destroy_sync(sbufs);

    // VkRenderingAttachmentInfo r_col_atts[]{
    //     Vks(VkRenderingAttachmentInfo{
    //         .imageView = output->md.vk->view,
    //         .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    //         .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    //         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    //         .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
    //     }),
    // };
    // auto rendering_info = Vks(VkRenderingInfo{
    //     .renderArea = { .extent = { .width = (uint32_t)fb_width, .height = (uint32_t)fb_height } },
    //     .layerCount = 1,
    //     .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
    //     .pColorAttachments = r_col_atts,
    // });

    // cmd->bind_index(index_buffer.get(), 0, VK_INDEX_TYPE_UINT16);
    // cmd->bind_pipeline(pipeline.get());
    //{
    //     float scale[2];
    //     scale[0] = 2.0f / draw_data->DisplaySize.x;
    //     scale[1] = 2.0f / draw_data->DisplaySize.y;
    //     float translate[2];
    //     translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
    //     translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
    //     cmd->push_constants(ShaderStage::ALL, scale, { 0, 8 });
    //     cmd->push_constants(ShaderStage::ALL, translate, { 8, 8 });
    // }

    //{
    //    cmd->bind_resource(4, vertex_buffer);
    //}

    // cmd->begin_rendering(rendering_info);
    //{
    //     VkViewport viewport;
    //     viewport.x = 0;
    //     viewport.y = 0;
    //     viewport.width = (float)fb_width;
    //     viewport.height = (float)fb_height;
    //     viewport.minDepth = 0.0f;
    //     viewport.maxDepth = 1.0f;
    //     cmd->set_viewports(&viewport, 1);
    // }

    // ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    // ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
    // for(int n = 0; n < draw_data->CmdListsCount; n++)
    //{
    //     const ImDrawList* draw_list = draw_data->CmdLists[n];
    //     for(int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
    //     {
    //         const ImDrawCmd* imcmd = &draw_list->CmdBuffer[cmd_i];

    //        // Project scissor/clipping rectangles into framebuffer space
    //        ImVec2 clip_min((imcmd->ClipRect.x - clip_off.x) * clip_scale.x,
    //                        (imcmd->ClipRect.y - clip_off.y) * clip_scale.y);
    //        ImVec2 clip_max((imcmd->ClipRect.z - clip_off.x) * clip_scale.x,
    //                        (imcmd->ClipRect.w - clip_off.y) * clip_scale.y);

    //        // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
    //        if(clip_min.x < 0.0f) { clip_min.x = 0.0f; }
    //        if(clip_min.y < 0.0f) { clip_min.y = 0.0f; }
    //        if(clip_max.x > fb_width) { clip_max.x = (float)fb_width; }
    //        if(clip_max.y > fb_height) { clip_max.y = (float)fb_height; }
    //        if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) { continue; }

    //        // Apply scissor/clipping rectangle
    //        VkRect2D scissor;
    //        scissor.offset.x = (int32_t)(clip_min.x);
    //        scissor.offset.y = (int32_t)(clip_min.y);
    //        scissor.extent.width = (uint32_t)(clip_max.x - clip_min.x);
    //        scissor.extent.height = (uint32_t)(clip_max.y - clip_min.y);
    //        cmd->set_scissors(&scissor, 1);

    //        cmd->bind_resource(5, Handle<Texture>{ (uint32_t)imcmd->GetTexID() - 1 });
    //        cmd->draw_indexed(imcmd->ElemCount, 1, imcmd->IdxOffset + global_idx_offset, imcmd->VtxOffset + global_vtx_offset, 0);
    //    }
    //    global_idx_offset += draw_list->IdxBuffer.Size;
    //    global_vtx_offset += draw_list->VtxBuffer.Size;
    //}
    // cmd->end_rendering();
}

void ImGuiRenderer::handle_imtexture(ImTextureData* imtex)
{
    if(imtex->Status == ImTextureStatus_OK) { return; }

    auto& r = get_renderer();
    Handle<Image> image;
    if(imtex->Status == ImTextureStatus_WantCreate)
    {
        image = r.make_image(Image::init("imgui image", (uint32_t)imtex->Width, (uint32_t)imtex->Height, ImageFormat::R8G8B8A8_UNORM,
                                         ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_DST_BIT, ImageLayout::READ_ONLY));
        // auto texture = r.make_texture(TextureDescriptor{ image->default_view, ImageLayout::READ_ONLY, false });
        images.push_back(image);
        // textures.push_back(texture);
        imtex->SetTexID((ImTextureID)(*image + 1)); // +1 so GetTexID doesn't complain when it's 0.
    }

    if(imtex->Status == ImTextureStatus_WantCreate || imtex->Status == ImTextureStatus_WantUpdates)
    {
        const int upload_x = (imtex->Status == ImTextureStatus_WantCreate) ? 0 : imtex->UpdateRect.x;
        const int upload_y = (imtex->Status == ImTextureStatus_WantCreate) ? 0 : imtex->UpdateRect.y;
        const int upload_w = (imtex->Status == ImTextureStatus_WantCreate) ? imtex->Width : imtex->UpdateRect.w;
        const int upload_h = (imtex->Status == ImTextureStatus_WantCreate) ? imtex->Height : imtex->UpdateRect.h;
        assert(image);
        assert(upload_w == imtex->Width && upload_h == imtex->Height);
        r.staging->copy(image, imtex->Pixels, 0, 0);
        assert(false);
        // r.sbuf->barrier(image, ImageLayout::READ_ONLY);afsdadsf
        imtex->SetStatus(ImTextureStatus_OK);
    }
}

} // namespace gfx
} // namespace eng
