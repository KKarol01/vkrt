#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/passes/rendergraph.hpp>
#include <eng/renderer/descpool.hpp>
#include <ImGuizmo/ImGuizmo.h>
#include "passes.hpp"

static void set_pc_vsm_common(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto slr = r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    auto depth_view = r.make_image_view(fd.gbuffer.depth_buffer_image);
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table);
    auto shadow_map = r.make_image_view(r.vsm.shadow_map_0);
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(fd.transform_buffers),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.vsm.free_allocs_buffer),
        r.get_bindless_index(depth_view, VK_IMAGE_LAYOUT_GENERAL, slr),
        r.get_bindless_index(page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr),
        r.get_bindless_index(fd.constants),
        0,
        r.get_bindless_index(shadow_map, VK_IMAGE_LAYOUT_GENERAL, nullptr),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(bindless_indices), bindless_indices);
}

static void set_pc_vsm_debug_copy(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table);
    auto page_view_rgb8 = r.make_image_view(r.vsm.dir_light_page_table_rgb8);
    uint32_t bindless_indices[]{
        r.get_bindless_index(page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr),
        r.get_bindless_index(page_view_rgb8, VK_IMAGE_LAYOUT_GENERAL, nullptr),
        r.get_bindless_index(r.vsm.constants_buffer),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(bindless_indices), bindless_indices);
}

static void set_pc_default_unlit(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto slr = r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    auto depth_view = r.make_image_view(fd.gbuffer.depth_buffer_image);
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table);
    auto shadow_map = r.make_image_view(r.vsm.shadow_map_0);
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(fd.transform_buffers),
        r.get_bindless_index(fd.constants),
        r.get_bindless_index(r.mesh_instances_buffer),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(shadow_map, VK_IMAGE_LAYOUT_GENERAL, nullptr),
        r.get_bindless_index(page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(bindless_indices), bindless_indices);
}

namespace rendergraph2 {

RenderPass::RenderPass(const std::string& name, const eng::rpp::PipelineSettings& settings)
    : name(name), pipeline(RendererVulkan::get_instance()->pipelines.get_pipeline(settings)) {}

ZPrepassPass::ZPrepassPass(RenderGraph* rg)
    : RenderPass("ZPrepassPass",
                 eng::rpp::PipelineSettings{
                     .settings = RasterizationSettings{ .num_col_formats = 0, .depth_test = true, .depth_write = true, .depth_op = VK_COMPARE_OP_LESS },
                     .shaders = { "vsm/zprepass.vert.glsl", "vsm/zprepass.frag.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return r->get_frame_data().gbuffer.depth_buffer_image; },
                                                       ResourceFlags::PER_FRAME_BIT),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL } };
}

void ZPrepassPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    const auto r_dep_att = Vks(VkRenderingAttachmentInfo{
        .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).get_view(),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .depthStencil = { 1.0f, 0 } },
    });
    const auto rendering_info = Vks(VkRenderingInfo{
        .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
                                    .height = (uint32_t)Engine::get().window->height } },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pColorAttachments = nullptr,
        .pDepthAttachment = &r_dep_att,
    });
    vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBeginRendering(cmd, &rendering_info);
    VkRect2D r_sciss_1 = rendering_info.renderArea;
    VkViewport r_view_1{ .x = 0.0f,
                         .y = 0.0f,
                         .width = (float)rendering_info.renderArea.extent.width,
                         .height = (float)rendering_info.renderArea.extent.height,
                         .minDepth = 0.0f,
                         .maxDepth = 1.0f };
    vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
    vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
    set_pc_vsm_common(cmd);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                  sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                  0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    vkCmdEndRendering(cmd);
}

VsmClearPagesPass::VsmClearPagesPass(RenderGraph* rg)
    : RenderPass("VsmClearPagesPass", eng::rpp::PipelineSettings{ .shaders = { "vsm/clear_page.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{
                     .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table; }),
                     .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                     .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                     .layout = VK_IMAGE_LAYOUT_GENERAL,
                 },
                 Access{
                     .resource = rg->make_resource([r] { return r->vsm.shadow_map_0; }),
                     .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                     .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 } };
}

void VsmClearPagesPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    set_pc_vsm_common(cmd);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
    VkClearColorValue clear_value{ .float32 = { 1.0f, 0.0f, 0.0f, 0.0f } };
    VkImageSubresourceRange clear_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };
    vkCmdClearColorImage(cmd, r.get_image(r.vsm.shadow_map_0).image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value,
                         1, &clear_range);
}

VsmPageAllocPass::VsmPageAllocPass(RenderGraph* rg)
    : RenderPass("VsmPageAllocPass", eng::rpp::PipelineSettings{ .shaders = { "vsm/page_alloc.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{
                     .resource = rg->make_resource([r] { return r->vsm.free_allocs_buffer; }),
                     .flags = AccessFlags::READ_WRITE_BIT,
                     .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 },
                 Access{ .resource = rg->make_resource([r] { return r->get_frame_data().gbuffer.depth_buffer_image; },
                                                       ResourceFlags::PER_FRAME_BIT),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } };
}

void VsmPageAllocPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    set_pc_vsm_common(cmd);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDispatch(cmd, (uint32_t)std::ceilf(Engine::get().window->width / 8.0f),
                  (uint32_t)std::ceilf(Engine::get().window->height / 8.0f), 1);
}

VsmShadowsPass::VsmShadowsPass(RenderGraph* rg)
    : RenderPass("VsmShadowsPass",
                 eng::rpp::PipelineSettings{
                     .settings = RasterizationSettings{ .num_col_formats = 0, .depth_test = false, .depth_write = false },
                     .shaders = { "vsm/shadow.vert.glsl", "vsm/shadow.frag.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->vsm.shadow_map_0; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void VsmShadowsPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    const auto rendering_info = Vks(VkRenderingInfo{
        .renderArea = { .extent = { .width = r.get_image(r.vsm.shadow_map_0).vk_info.extent.width,
                                    .height = r.get_image(r.vsm.shadow_map_0).vk_info.extent.height } },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pColorAttachments = nullptr,
        .pDepthAttachment = nullptr,
    });
    vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBeginRendering(cmd, &rendering_info);
    VkRect2D r_sciss_1 = rendering_info.renderArea;
    VkViewport r_view_1{ .x = 0.0f,
                         .y = 0.0f,
                         .width = (float)rendering_info.renderArea.extent.width,
                         .height = (float)rendering_info.renderArea.extent.height,
                         .minDepth = 0.0f,
                         .maxDepth = 1.0f };
    vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
    vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
    set_pc_vsm_common(cmd);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                  sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                  0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    vkCmdEndRendering(cmd);
}

VsmDebugPageCopyPass::VsmDebugPageCopyPass(RenderGraph* rg)
    : RenderPass("VsmDebugPageCopyPass",
                 eng::rpp::PipelineSettings{ .shaders = { "vsm/debug_page_alloc_copy.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table_rgb8; }),
                         .flags = AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void VsmDebugPageCopyPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    set_pc_vsm_debug_copy(cmd);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
}

DefaultUnlitPass::DefaultUnlitPass(RenderGraph* rg)
    : RenderPass("DefaultUnlitPass",
                 eng::rpp::PipelineSettings{
                     .settings = RasterizationSettings{ .depth_test = true, .depth_write = false, .depth_op = VK_COMPARE_OP_LESS_OR_EQUAL },
                     .shaders = { "default_unlit/unlit.vert.glsl", "default_unlit/unlit.frag.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return r->get_frame_data().gbuffer.color_image; }, ResourceFlags::PER_FRAME_BIT),
                         .flags = AccessFlags::WRITE_BIT | AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                         .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                 Access{ .resource = rg->make_resource([r] { return r->get_frame_data().gbuffer.depth_buffer_image; },
                                                       ResourceFlags::PER_FRAME_BIT),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                 Access{ .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->vsm.shadow_map_0; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void DefaultUnlitPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto r_col_att_1 = Vks(VkRenderingAttachmentInfo{
        .imageView = r.get_image(r.get_frame_data().gbuffer.color_image).get_view(),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
    });

    VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };
    auto r_dep_att = Vks(VkRenderingAttachmentInfo{
        .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).get_view(),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_NONE,
        .clearValue = { .depthStencil = { 1.0f, 0 } },
    });
    auto rendering_info = Vks(VkRenderingInfo{
        .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
                                    .height = (uint32_t)Engine::get().window->height } },
        .layerCount = 1,
        .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
        .pColorAttachments = r_col_atts,
        .pDepthAttachment = &r_dep_att,
    });

    vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBeginRendering(cmd, &rendering_info);
    VkRect2D r_sciss_1{ .offset = {}, .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
    VkViewport r_view_1{
        .x = 0.0f, .y = 0.0f, .width = Engine::get().window->width, .height = Engine::get().window->height, .minDepth = 0.0f, .maxDepth = 1.0f
    };
    vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
    vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
    set_pc_default_unlit(cmd);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                  sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                  0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    vkCmdEndRendering(cmd);
}

ImguiPass::ImguiPass(RenderGraph* rg) : RenderPass("ImguiPass", {}) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return swapchain_handle; }, ResourceFlags::PER_FRAME_BIT),
                         .flags = AccessFlags::WRITE_BIT | AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                         .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                 Access{ .resource = rg->make_resource([r] { return r->get_frame_data().gbuffer.color_image; }, ResourceFlags::PER_FRAME_BIT),
                         .flags = AccessFlags::READ_BIT | AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                 Access{ .resource = rg->make_resource([r] { return r->vsm.dir_light_page_table_rgb8; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                 Access{ .resource = rg->make_resource([r] { return r->vsm.shadow_map_0; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } };
}

void ImguiPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    ImGui::SetCurrentContext(Engine::get().ui_ctx->imgui_ctx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    eng_ui_update();
    ImGui::Render();
    ImDrawData* im_draw_data = ImGui::GetDrawData();
    if(im_draw_data) {
        VkRenderingAttachmentInfo r_col_atts[]{
            Vks(VkRenderingAttachmentInfo{
                .imageView = RendererVulkan::get_instance()->swapchain.get_current_view(),
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
            }),
        };
        VkRect2D r_sciss_1{ .offset = {},
                            .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
        VkViewport r_view_1{ .x = 0.0f,
                             .y = Engine::get().window->height,
                             .width = Engine::get().window->width,
                             .height = Engine::get().window->height,
                             .minDepth = 0.0f,
                             .maxDepth = 1.0f };
        auto rendering_info = Vks(VkRenderingInfo{
            .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
                                        .height = (uint32_t)Engine::get().window->height } },
            .layerCount = 1,
            .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
            .pColorAttachments = r_col_atts,
        });
        vkCmdBeginRendering(cmd, &rendering_info);
        vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
        vkCmdSetViewport(cmd, 0, 1, &r_view_1);
        ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
        vkCmdEndRendering(cmd);
    }
}

SwapchainPresentPass::SwapchainPresentPass(RenderGraph* rg) : RenderPass("SwapchainPresentPass", {}) {
    auto r = RendererVulkan::get_instance();
    accesses = {
        Access{ .resource = rg->make_resource([r] { return swapchain_handle; }, ResourceFlags::PER_FRAME_BIT),
                .flags = AccessFlags::NONE_BIT,
                .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_2_NONE,
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
    };
}

void SwapchainPresentPass::render(VkCommandBuffer cmd) {}

} // namespace rendergraph2