#include "passes.hpp"
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/passes/rendergraph.hpp>

namespace rendergraph2 {

RenderPass::RenderPass(const std::string& name, const std::vector<std::filesystem::path>& shaders,
                       const pipeline_settings_t& pipeline_settings) {}

VsmClearPagesPass::VsmClearPagesPass(RenderGraph* rg)
    : RenderPass("vsm_clear_page_pass", { "vsm/clear_page.comp.glsl" }, RasterizationPipelineSettings{}) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{
                     .resource =
                         rg->make_resource(r->vsm.dir_light_page_table, [r] { return r->vsm.dir_light_page_table; }),
                     .flags = ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                     .type = AccessFlags::WRITE_BIT,
                     .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                     .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     .dst_layout = VK_IMAGE_LAYOUT_GENERAL,
                 },
                 Access{
                     .resource = rg->make_resource(r->vsm.shadow_map_0, [r] { return r->vsm.shadow_map_0; }),
                     .flags = ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                     .type = AccessFlags::WRITE_BIT,
                     .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     .dst_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 } };
    depth_buffer = r->make_image_view(r->)
}

void VsmClearPagesPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(r.get_frame_data().transform_buffers),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.vsm.free_allocs_buffer),
        r.get_bindless_index(r.get_frame_data().gbuffer.view_depth_buffer_image_ronly_lr),
        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
        r.get_bindless_index(r.get_frame_data().constants),
        0,
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
    VkClearColorValue clear_value{ .float32 = { 1.0f, 0.0f, 0.0f, 0.0f } };
    VkImageSubresourceRange clear_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };
    vkCmdClearColorImage(cmd, r.get_image(r.vsm.shadow_map_0).image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value,
                         1, &clear_range);
}

} // namespace rendergraph2