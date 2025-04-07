#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/passes/rendergraph.hpp>
#include <eng/renderer/descpool.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include "renderpass.hpp" // has to at the bottom, or otherwise won't compile (on msvc at least)

using namespace rendergraph;

RenderPass::RenderPass(const std::string& name, const std::vector<Access>& accesses, const PipelineSettings& pipeline_settings) noexcept
    : name(name), accesses(accesses),
      pipeline(RendererVulkan::get_instance()->pipeline_compiler.get_pipeline(pipeline_settings)) {}

VsmClearPagesPass::VsmClearPagesPass(RenderGraph* rg) noexcept
    : RenderPass("vsm_clear_pages_pass",
                 { Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table }),
                           .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                           .type = AccessType::WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_GENERAL },
                   Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.shadow_map_0 }),
                           .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                           .type = AccessType::WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL } },
                 PipelineSettings{ .shaders = { "vsm/clear_page.comp.glsl" } }) {
    auto& r = *RendererVulkan::get_instance();
    vsm_dir_light_page_table = r.make_image_view(r.vsm.dir_light_page_table, VK_IMAGE_LAYOUT_GENERAL, nullptr);
}

void VsmClearPagesPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.vsm.free_allocs_buffer),
        r.get_bindless_index(vsm_dir_light_page_table),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
    VkClearColorValue clear_value{ .float32 = { 1.0f, 0.0f, 0.0f, 0.0f } };
    VkImageSubresourceRange clear_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };
    vkCmdClearColorImage(cmd, r.get_image(r.vsm.shadow_map_0).image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value,
                         1, &clear_range);
}

VsmPageAllocPass::VsmPageAllocPass(RenderGraph* rg) noexcept
    : RenderPass("vsm_page_alloc_pass",
                 { Access{ .resource = rg->get_resource(Resource{
                               RendererVulkan::get_instance()->get_frame_data().gbuffer.depth_buffer_image,
                               RendererVulkan::get_instance()->get_frame_data(1).gbuffer.depth_buffer_image }),
                           .type = AccessType::READ_BIT,
                           .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT,
                           .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                   Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table }),
                           .type = AccessType::READ_WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_GENERAL },
                   Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.free_allocs_buffer }),
                           .type = AccessType::READ_WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT } },
                 PipelineSettings{ .shaders = { "vsm/page_alloc.comp.glsl" } }) {
    auto& r = *RendererVulkan::get_instance();
    depth_buffer.data = { r.make_image_view(r.get_frame_data().gbuffer.depth_buffer_image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                            r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT)),
                          r.make_image_view(r.get_frame_data(1).gbuffer.depth_buffer_image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                            r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT)) };
    vsm_dir_light_page_table = r.make_image_view(r.vsm.dir_light_page_table, VK_IMAGE_LAYOUT_GENERAL, nullptr);
}

void VsmPageAllocPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(r.get_frame_data().transform_buffers),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.vsm.free_allocs_buffer),
        r.get_bindless_index(depth_buffer.get_swap()),
        r.get_bindless_index(vsm_dir_light_page_table),
        r.get_bindless_index(r.get_frame_data().constants),
        0,
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDispatch(cmd, (uint32_t)std::ceilf(Engine::get().window->width / 8.0f),
                  (uint32_t)std::ceilf(Engine::get().window->height / 8.0f), 1);
}

ZPrepassPass::ZPrepassPass(RenderGraph* rg) noexcept
    : RenderPass("z_prepass",
                 { Access{ .resource = rg->get_resource(Resource{
                               RendererVulkan::get_instance()->get_frame_data().gbuffer.depth_buffer_image,
                               RendererVulkan::get_instance()->get_frame_data(1).gbuffer.depth_buffer_image }),
                           .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                           .type = AccessType::READ_WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                           .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL } },
                 PipelineSettings{
                     .settings = RasterizationSettings{ .num_col_formats = 0, .depth_test = true, .depth_write = true, .depth_op = VK_COMPARE_OP_LESS },
                     .shaders = { "vsm/zprepass.vert.glsl", "vsm/zprepass.frag.glsl" } }) {}

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
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(r.get_frame_data().transform_buffers),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                  sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                  0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    vkCmdEndRendering(cmd);
}

VsmShadowPass::VsmShadowPass(RenderGraph* rg) noexcept
    : RenderPass("vsm_shadow_pass",
                 {
                     Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table }),
                             .type = AccessType::READ_BIT,
                             .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             .access = VK_ACCESS_2_SHADER_READ_BIT,
                             .layout = VK_IMAGE_LAYOUT_GENERAL },
                     Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.shadow_map_0 }),
                             .type = AccessType::READ_WRITE_BIT,
                             .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                             .layout = VK_IMAGE_LAYOUT_GENERAL },
                     Access{ .resource = rg->get_resource(Resource{
                                 RendererVulkan::get_instance()->get_frame_data().gbuffer.depth_buffer_image,
                                 RendererVulkan::get_instance()->get_frame_data(1).gbuffer.depth_buffer_image }),
                             .type = rendergraph::AccessType::READ_BIT,
                             .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             .access = VK_ACCESS_2_SHADER_READ_BIT,
                             .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                 },
                 PipelineSettings{ .settings = RasterizationSettings{ .num_col_formats = 0, .depth_test = false, .depth_write = false },
                                   .shaders = { "vsm/shadow.vert.glsl", "vsm/shadow.frag.glsl" } }) {
    auto& r = *RendererVulkan::get_instance();
    depth_buffer.data = { r.make_image_view(r.get_frame_data().gbuffer.depth_buffer_image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                            r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT)),
                          r.make_image_view(r.get_frame_data(1).gbuffer.depth_buffer_image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                            r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT)) };
    vsm_dir_light_page_table = r.make_image_view(r.vsm.dir_light_page_table, VK_IMAGE_LAYOUT_GENERAL, nullptr);
    vsm_shadow_map_0 = r.make_image_view(r.vsm.shadow_map_0, VK_IMAGE_LAYOUT_GENERAL, nullptr);
}

void VsmShadowPass::render(VkCommandBuffer cmd) {
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
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(r.get_frame_data().transform_buffers),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.vsm.free_allocs_buffer),
        r.get_bindless_index(depth_buffer.get_swap()),
        r.get_bindless_index(vsm_dir_light_page_table),
        r.get_bindless_index(r.get_frame_data().constants),
        0,
        r.get_bindless_index(vsm_shadow_map_0),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                  sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                  0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    vkCmdEndRendering(cmd);
}

DefaultUnlitPass::DefaultUnlitPass(RenderGraph* rg) noexcept
    : RenderPass(
          "default_unlit_pass",
          { Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->get_frame_data().gbuffer.color_image,
                                                           RendererVulkan::get_instance()->get_frame_data(1).gbuffer.color_image }),
                    .flags = rendergraph::AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                    .type = rendergraph::AccessType::WRITE_BIT,
                    .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
            Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->get_frame_data().gbuffer.depth_buffer_image,
                                                           RendererVulkan::get_instance()->get_frame_data(1).gbuffer.depth_buffer_image }),
                    .type = rendergraph::AccessType::READ_BIT,
                    .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
            Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.shadow_map_0 }),
                    .type = rendergraph::AccessType::READ_BIT,
                    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_GENERAL },
            Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table }),
                    .type = rendergraph::AccessType::READ_BIT,
                    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_GENERAL } },
          PipelineSettings{ .settings = RasterizationSettings{ .num_col_formats = 0, .depth_test = true, .depth_write = false, .depth_op = VK_COMPARE_OP_LESS_OR_EQUAL },
                            .shaders = { "default_unlit/unlit.vert.glsl", "default_unlit/unlit.frag.glsl" } }) {
    auto& r = *RendererVulkan::get_instance();
    vsm_dir_light_page_table = r.make_image_view(r.vsm.dir_light_page_table, VK_IMAGE_LAYOUT_GENERAL, nullptr);
    vsm_shadow_map_0 = r.make_image_view(r.vsm.shadow_map_0, VK_IMAGE_LAYOUT_GENERAL, nullptr);
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
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(r.get_frame_data().transform_buffers),
        r.get_bindless_index(r.get_frame_data().constants),
        r.get_bindless_index(r.mesh_instances_buffer),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(vsm_shadow_map_0),
        r.get_bindless_index(vsm_dir_light_page_table),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                  sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                  0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    vkCmdEndRendering(cmd);
}

VsmDebugPageAllocCopy::VsmDebugPageAllocCopy(RenderGraph* rg) noexcept
    : RenderPass("vsm_debug_page_alloc_copy_pass",
                 { Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table }),
                           .type = rendergraph::AccessType::READ_BIT,
                           .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT,
                           .layout = VK_IMAGE_LAYOUT_GENERAL },
                   Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table_rgb8 }),
                           .type = rendergraph::AccessType::WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_GENERAL } },
                 PipelineSettings{ .shaders = { "vsm/debug_page_alloc_copy.comp.glsl" } }) {
    auto& r = *RendererVulkan::get_instance();
    vsm_dir_light_page_table = r.make_image_view(r.vsm.dir_light_page_table, VK_IMAGE_LAYOUT_GENERAL, nullptr);
    vsm_dir_light_page_table_rgba8 = r.make_image_view(r.vsm.dir_light_page_table_rgb8, VK_IMAGE_LAYOUT_GENERAL, nullptr);
}

void VsmDebugPageAllocCopy::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    uint32_t bindless_indices[]{
        r.get_bindless_index(vsm_dir_light_page_table),
        r.get_bindless_index(vsm_dir_light_page_table_rgba8),
        r.get_bindless_index(r.vsm.constants_buffer),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
}

ImguiPass::ImguiPass(RenderGraph* rg) noexcept
    : RenderPass("imgui_pass",
                 { Access{ .resource = rg->get_resource(Resource{ { Handle<Image>{ swapchain_index }, Handle<Image>{ swapchain_index } },
                                                                  ResourceFlags::SWAPCHAIN_IMAGE_BIT }),
                           .flags = rendergraph::AccessFlags::FROM_UNDEFINED_LAYOUT_BIT,
                           .type = rendergraph::AccessType::WRITE_BIT,
                           .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                           .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                   Access{ .resource =
                               rg->get_resource(Resource{ RendererVulkan::get_instance()->get_frame_data().gbuffer.color_image,
                                                          RendererVulkan::get_instance()->get_frame_data(1).gbuffer.color_image }),
                           .type = rendergraph::AccessType::READ_BIT,
                           .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT,
                           .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                   Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.dir_light_page_table_rgb8 }),
                           .type = rendergraph::AccessType::READ_BIT,
                           .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT,
                           .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                   Access{ .resource = rg->get_resource(Resource{ RendererVulkan::get_instance()->vsm.shadow_map_0 }),
                           .type = rendergraph::AccessType::READ_BIT,
                           .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           .access = VK_ACCESS_2_SHADER_READ_BIT,
                           .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } },
                 PipelineSettings{}) {}

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
                .imageView = RendererVulkan::get_instance()->swapchain.get_current_image().get_view(),
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

SwapchainPresentPass::SwapchainPresentPass(RenderGraph* rg) noexcept
    : RenderPass("swapchain_preset_pass",
                 { Access{ .resource = rg->get_resource(Resource{ { Handle<Image>{ swapchain_index }, Handle<Image>{ swapchain_index } },
                                                                  ResourceFlags::SWAPCHAIN_IMAGE_BIT }),
                           .type = rendergraph::AccessType::NONE_BIT,
                           .stage = VK_PIPELINE_STAGE_2_NONE,
                           .access = VK_ACCESS_2_NONE,
                           .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR } },
                 PipelineSettings{}) {}

void SwapchainPresentPass::render(VkCommandBuffer cmd) {}
