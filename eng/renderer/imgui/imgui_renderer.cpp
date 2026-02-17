#include "imgui_renderer.hpp"
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/engine.hpp>
#include <third_party/imgui/imgui.h>
#include <third_party/imgui/imgui_internal.h>
#include <third_party/imgui/backends/imgui_impl_glfw.h>
#include <third_party/imgui/backends/imgui_impl_vulkan.h>
#include <third_party/ImGuizmo/ImGuizmo.h>
#include <eng/renderer/vulkan_structs.hpp>

namespace eng
{
namespace gfx
{

void ImGuiRenderer::init()
{
    auto& r = get_renderer();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(Engine::get().window->window, true);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniSavingRate = 1.0f;

    ImGui::LoadIniSettingsFromDisk("imgui.ini");

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    pipeline = r.make_pipeline(PipelineCreateInfo{
        .shaders = { r.make_shader("imgui/imgui.vert.glsl"), r.make_shader("imgui/imgui.frag.glsl") },
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

    vertex_buffer = r.make_buffer(Buffer::init("imgui vertex buffer", 1024 * 1024, BufferUsage::STORAGE_BIT));
    index_buffer = r.make_buffer(Buffer::init("imgui index buffer", 1024 * 1024, BufferUsage::INDEX_BIT));
}

void ImGuiRenderer::update(RenderGraph* graph, Handle<RenderGraph::ResourceAccess> output)
{
    auto& r = get_renderer();

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ui_callbacks.signal();

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
    for(int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        r.staging->copy(vertex_buffer, draw_list->VtxBuffer.Data, vtx_off, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        r.staging->copy(index_buffer, draw_list->IdxBuffer.Data, idx_off, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_off += draw_list->VtxBuffer.Size * sizeof(ImDrawVert);
        idx_off += draw_list->IdxBuffer.Size * sizeof(ImDrawIdx);
    }

    struct ImPassData
    {
    };
    graph->add_graphics_pass<ImPassData>(
        "imgui",
        [this, output](RenderGraph::PassBuilder& builder) {
            get_renderer().imgui_input = *builder.access_color(output);
            this->output = get_renderer().imgui_input;
            return ImPassData{};
        },
        [this, &r](RenderGraph& graph, RenderGraph::PassBuilder& builder, const ImPassData&) {
            auto* cmd = builder.open_cmd_buf();
            cmd->wait_sync(r.staging->get_wait_sem());
            ImDrawData* draw_data = ImGui::GetDrawData();

            Handle<RenderGraph::ResourceAccess> output{ this->output };
            const auto& img = graph.get_res(output).as_image().get();

            VkRenderingAttachmentInfo r_col_atts[]{
                Vks(VkRenderingAttachmentInfo{
                    .imageView = graph.get_acc(output).image_view.get_md().vk->view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = { .color = { .uint32 = { 0, 0, 0, 0 } } },
                }),
            };

            auto rendering_info = Vks(VkRenderingInfo{
                .renderArea = { .extent = { .width = img.width, .height = img.height } },
                .layerCount = 1,
                .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
                .pColorAttachments = r_col_atts,
            });

            cmd->bind_index(index_buffer.get(), 0, VK_INDEX_TYPE_UINT16);
            cmd->bind_pipeline(pipeline.get());
            {
                float scale[2];
                scale[0] = 2.0f / draw_data->DisplaySize.x;
                scale[1] = 2.0f / draw_data->DisplaySize.y;
                float translate[2];
                translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
                translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
                cmd->push_constants(ShaderStage::ALL, scale, { 0, 8 });
                cmd->push_constants(ShaderStage::ALL, translate, { 8, 8 });
            }

            {
                DescriptorResource bindresources[]{ DescriptorResource::as_storage(4, BufferView::init(vertex_buffer)) };
                cmd->bind_set(0, bindresources);
            }

            cmd->begin_rendering(rendering_info);
            {
                VkViewport viewport;
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = (float)img.width;
                viewport.height = (float)img.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                cmd->set_viewports(&viewport, 1);
            }

            ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
            ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
            int global_vtx_offset = 0;
            int global_idx_offset = 0;
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
                    if(clip_max.x > img.width) { clip_max.x = (float)img.width; }
                    if(clip_max.y > img.height) { clip_max.y = (float)img.height; }
                    if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) { continue; }

                    // Apply scissor/clipping rectangle
                    VkRect2D scissor;
                    scissor.offset.x = (int32_t)(clip_min.x);
                    scissor.offset.y = (int32_t)(clip_min.y);
                    scissor.extent.width = (uint32_t)(clip_max.x - clip_min.x);
                    scissor.extent.height = (uint32_t)(clip_max.y - clip_min.y);
                    cmd->set_scissors(&scissor, 1);
                    DescriptorResource bindresources[]{
                        DescriptorResource::as_sampled(5, ImageView::init(Handle<Image>{ (uint32_t)imcmd->GetTexID() - 1 }))
                    };
                    cmd->bind_set(1, bindresources);
                    cmd->draw_indexed(imcmd->ElemCount, 1, imcmd->IdxOffset + global_idx_offset,
                                      imcmd->VtxOffset + global_vtx_offset, 0);
                }
                global_idx_offset += draw_list->IdxBuffer.Size;
                global_vtx_offset += draw_list->VtxBuffer.Size;
            }
            cmd->end_rendering();
        });
}

void ImGuiRenderer::handle_imtexture(ImTextureData* imtex)
{
    if(imtex->Status == ImTextureStatus_OK) { return; }

    auto& r = get_renderer();
    Handle<Image> image;
    if(imtex->Status == ImTextureStatus_WantCreate)
    {
        image = r.make_image(Image::init("imgui image", (uint32_t)imtex->Width, (uint32_t)imtex->Height,
                                         ImageFormat::R8G8B8A8_UNORM, ImageUsage::SAMPLED_BIT, ImageLayout::READ_ONLY));
        images.push_back(image);
        imtex->SetTexID((ImTextureID)(*image + 1)); // +1 so GetTexID doesn't complain when it's 0.
    }

    if(imtex->Status == ImTextureStatus_WantCreate || imtex->Status == ImTextureStatus_WantUpdates)
    {
        const int upload_x = (imtex->Status == ImTextureStatus_WantCreate) ? 0 : imtex->UpdateRect.x;
        const int upload_y = (imtex->Status == ImTextureStatus_WantCreate) ? 0 : imtex->UpdateRect.y;
        const int upload_w = (imtex->Status == ImTextureStatus_WantCreate) ? imtex->Width : imtex->UpdateRect.w;
        const int upload_h = (imtex->Status == ImTextureStatus_WantCreate) ? imtex->Height : imtex->UpdateRect.h;
        ENG_ASSERT(image);
        ENG_ASSERT(upload_x == 0 && upload_y == 0 && upload_w == imtex->Width && upload_h == imtex->Height);
        r.staging->copy(image, imtex->Pixels, 0, 0);
        imtex->SetStatus(ImTextureStatus_OK);
    }
}

} // namespace gfx
} // namespace eng
