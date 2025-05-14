#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/passes/rendergraph.hpp>
#include <eng/renderer/descpool.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <assets/shaders/bindless_structures.inc.glsl>
#include <ImGuizmo/ImGuizmo.h>
#include <random>
#include <array>
#include "passes.hpp"

namespace gfx {

static void set_pc_vsm_common(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto slr = r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    auto depth_view = r.make_image_view(fd.gbuffer.depth_buffer_image);
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table,
                                       Vks(VkImageViewCreateInfo{
                                           .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                           .format = RendererVulkan::get_image(r.vsm.dir_light_page_table).vk_info.format }));
    auto shadow_map = r.make_image_view(r.vsm.shadow_map_0);
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(fd.transform_buffers),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.vsm.free_allocs_buffer),
        r.get_bindless_index(r.make_texture(fd.gbuffer.depth_buffer_image, depth_view, VK_IMAGE_LAYOUT_GENERAL, slr)),
        r.get_bindless_index(r.make_texture(r.vsm.dir_light_page_table, page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        r.get_bindless_index(fd.constants),
        0,
        r.get_bindless_index(r.make_texture(r.vsm.shadow_map_0, shadow_map, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(bindless_indices), bindless_indices);
}

static void set_pc_vsm_shadows(VkCommandBuffer cmd, int cascade_index) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table,
                                       Vks(VkImageViewCreateInfo{
                                           .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                           .format = RendererVulkan::get_image(r.vsm.dir_light_page_table).vk_info.format }));
    auto shadow_map = r.make_image_view(r.vsm.shadow_map_0);
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(fd.transform_buffers),
        r.get_bindless_index(fd.constants),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.make_texture(r.vsm.dir_light_page_table, page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        r.get_bindless_index(r.make_texture(r.vsm.shadow_map_0, shadow_map, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        cascade_index,
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(bindless_indices), bindless_indices);
}

static void set_pc_vsm_debug_copy(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table,
                                       Vks(VkImageViewCreateInfo{
                                           .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                           .format = RendererVulkan::get_image(r.vsm.dir_light_page_table).vk_info.format }));
    auto page_view_rgb8 =
        r.make_image_view(r.vsm.dir_light_page_table_rgb8,
                          Vks(VkImageViewCreateInfo{
                              .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                              .format = RendererVulkan::get_image(r.vsm.dir_light_page_table_rgb8).vk_info.format }));
    uint32_t bindless_indices[]{
        r.get_bindless_index(r.make_texture(r.vsm.dir_light_page_table, page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        r.get_bindless_index(r.make_texture(r.vsm.dir_light_page_table_rgb8, page_view_rgb8, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        r.get_bindless_index(r.vsm.constants_buffer),
    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(bindless_indices), bindless_indices);
}

static void set_pc_default_unlit(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    auto& fd = r.get_frame_data();
    auto slr = r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    auto depth_view = r.make_image_view(fd.gbuffer.depth_buffer_image);
    auto page_view = r.make_image_view(r.vsm.dir_light_page_table,
                                       Vks(VkImageViewCreateInfo{
                                           .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                           .format = RendererVulkan::get_image(r.vsm.dir_light_page_table).vk_info.format }));
    auto shadow_map = r.make_image_view(r.vsm.shadow_map_0);
    struct PushConstants {
        uint32_t index_buffer;
        uint32_t vertex_pos_buffer;
        uint32_t vertex_attrib_buffer;
        uint32_t transform_buffer;
        uint32_t frame_constants_buffer;
        uint32_t instance_mesh_buffer;
        uint32_t vsm_constants_buffer;
        uint32_t vsm_sm0_image;
        uint32_t vsm_dl_pt_image;
        uint32_t fft_hxy_image;
        uint32_t fft_hxx_image;
        uint32_t fft_hxz_image;
        uint32_t fft_hxn_image;
        float fft_lambda;
    };
    PushConstants pc = {
        r.get_bindless_index(r.index_buffer),
        r.get_bindless_index(r.vertex_positions_buffer),
        r.get_bindless_index(r.vertex_attributes_buffer),
        r.get_bindless_index(fd.transform_buffers),
        r.get_bindless_index(fd.constants),
        r.get_bindless_index(r.mesh_instances_buffer),
        r.get_bindless_index(r.vsm.constants_buffer),
        r.get_bindless_index(r.make_texture(r.vsm.shadow_map_0, shadow_map, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        r.get_bindless_index(r.make_texture(r.vsm.dir_light_page_table, page_view, VK_IMAGE_LAYOUT_GENERAL, nullptr)),
        r.get_bindless_index(r.make_texture(r.fftocean.debug_hx_image, VK_IMAGE_LAYOUT_GENERAL,
                                            r.samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT))),
        r.get_bindless_index(r.make_texture(r.fftocean.debug_hxx_image, VK_IMAGE_LAYOUT_GENERAL,
                                            r.samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT))),
        r.get_bindless_index(r.make_texture(r.fftocean.debug_hxz_image, VK_IMAGE_LAYOUT_GENERAL,
                                            r.samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT))),
        r.get_bindless_index(r.make_texture(r.fftocean.debug_hn_image, VK_IMAGE_LAYOUT_GENERAL,
                                            r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT))),
        r.fftocean.settings.disp_lambda,

    };
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0ull, sizeof(pc), &pc);
}

RenderPass::RenderPass(const std::string& name, const PipelineSettings& settings)
    : name(name), pipeline(RendererVulkan::get_instance()->pipelines.get_pipeline(settings)) {}

FFTOceanButterflyPass::FFTOceanButterflyPass(RenderGraph* rg)
    : RenderPass("FFTOceanInitPass", PipelineSettings{ .shaders = { "fftocean/butterfly.comp.glsl" } }) {
    auto& r = *RendererVulkan::get_instance();
    if(!r.fftocean.butterfly_image) {
        const auto ns = (uint32_t)r.fftocean.settings.num_samples;
        const auto logn = (uint32_t)(std::log2f(ns));
        r.fftocean.butterfly_image =
            r.make_image("fftocean/butterfly", Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                                      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                                      .extent = { logn, ns, 1u },
                                                                      .mipLevels = 1u,
                                                                      .arrayLayers = 1u,
                                                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                                                      .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
        r.fftocean.gaussian_distribution_image =
            r.make_image("fftocean/gaussian_distribution",
                         Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                .extent = { ns, ns, 1u },
                                                .mipLevels = 1u,
                                                .arrayLayers = 1u,
                                                .samples = VK_SAMPLE_COUNT_1_BIT,
                                                .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT }));
        std::vector<float> gaussian_distr(r.fftocean.settings.num_samples * r.fftocean.settings.num_samples * 4u);
        std::random_device dev;
        std::mt19937 mt{ dev() };
        std::normal_distribution nd{ 0.0f, 1.0f };
        for(auto i = 0u; i < gaussian_distr.size(); ++i) {
            gaussian_distr[i] = std::clamp(nd(mt), -1.0f, 1.0f);
        }
        r.staging_buffer
            ->send_to(r.fftocean.gaussian_distribution_image, VK_IMAGE_LAYOUT_GENERAL,
                      Vks(VkBufferImageCopy2{
                          .imageSubresource = VkImageSubresourceLayers{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
                          .imageExtent = { (uint32_t)r.fftocean.settings.num_samples, (uint32_t)r.fftocean.settings.num_samples, 1u },
                      }),
                      gaussian_distr)
            .submit_wait();
    }

    accesses = { Access{ .resource = rg->make_resource([&r] { return r.fftocean.butterfly_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanButterflyPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    // if(r.fftocean.passes_run++ > 1) { return; }
    auto& img = r.get_image(r.fftocean.butterfly_image);
    auto view = r.make_image_view(r.fftocean.butterfly_image);
    uint32_t pc[]{ r.get_bindless_index(r.make_texture(r.fftocean.butterfly_image, view, VK_IMAGE_LAYOUT_GENERAL, nullptr)) };
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), pc);
    vkCmdDispatch(cmd, img.vk_info.extent.width, img.vk_info.extent.height, 1u);
}

FFTOceanAmplitudesPass::FFTOceanAmplitudesPass(RenderGraph* rg)
    : RenderPass("FFTOceanAmplitudesPass", PipelineSettings{ .shaders = { "fftocean/amplitudes.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    if(!r->fftocean.amplitudes_image) {
        r->fftocean.amplitudes_image =
            r->make_image("fftocean/amplitudes",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.gaussian_distribution_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.amplitudes_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanAmplitudesPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    // if(r.fftocean.passes_run++ > 2) { return; }
    std::array<std::byte, sizeof(FFTOcean) + 2 * sizeof(Handle<Image>)> pc;
    static_assert(sizeof(pc) < 128);
    {
        const auto view1 = r.get_bindless_index(r.make_texture(r.fftocean.amplitudes_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        const auto view2 =
            r.get_bindless_index(r.make_texture(r.fftocean.gaussian_distribution_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        std::memcpy(pc.data(), &r.fftocean.settings, sizeof(r.fftocean.settings));
        std::memcpy(pc.data() + sizeof(r.fftocean.settings) + 0 * sizeof(Handle<Image>), &view1, sizeof(view1));
        std::memcpy(pc.data() + sizeof(r.fftocean.settings) + 1 * sizeof(Handle<Image>), &view2, sizeof(view2));
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, pc.size(), pc.data());
    uint32_t dispatch_size = r.get_image(r.fftocean.amplitudes_image).vk_info.extent.width / 8u;
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanFourierAmplitudesPass::FFTOceanFourierAmplitudesPass(RenderGraph* rg)
    : RenderPass("FFTOceanFourierAmplitudesPass",
                 PipelineSettings{ .shaders = { "fftocean/fourier_amps.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    if(!r->fftocean.fourier_amplitudes_image) {
        r->fftocean.fourier_amplitudes_image =
            r->make_image("fftocean/fourier_amplitudes",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
        r->fftocean.pingpong_image =
            r->make_image("fftocean/pingpong", Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                                      .format = VK_FORMAT_R32G32_SFLOAT,
                                                                      .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                                                  (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                                      .mipLevels = 1u,
                                                                      .arrayLayers = 1u,
                                                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                                                      .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.fourier_amplitudes_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.amplitudes_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanFourierAmplitudesPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    std::array<std::byte, sizeof(FFTOcean) + 2 * sizeof(Handle<Image>) + sizeof(float)> pc;
    static_assert(sizeof(pc) < 128);
    {
        const auto view1 = r.get_bindless_index(r.make_texture(r.fftocean.amplitudes_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        const auto view2 =
            r.get_bindless_index(r.make_texture(r.fftocean.fourier_amplitudes_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        float time = (float)glfwGetTime() * 0.001;
        std::memcpy(pc.data(), &r.fftocean.settings, sizeof(r.fftocean.settings));
        std::memcpy(pc.data() + sizeof(r.fftocean.settings) + 0 * sizeof(Handle<Image>), &view1, sizeof(view1));
        std::memcpy(pc.data() + sizeof(r.fftocean.settings) + 1 * sizeof(Handle<Image>), &view2, sizeof(view2));
        std::memcpy(pc.data() + sizeof(r.fftocean.settings) + 2 * sizeof(Handle<Image>), &time, sizeof(float));
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, pc.size(), pc.data());
    uint32_t dispatch_size = r.get_image(r.fftocean.amplitudes_image).vk_info.extent.width / 8u;
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanFourierButterflyPass::FFTOceanFourierButterflyPass(RenderGraph* rg)
    : RenderPass("FFTOceanFourierButterflyPass", PipelineSettings{ .shaders = { "fftocean/pingpong.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();
    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.fourier_amplitudes_image; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.pingpong_image; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.butterfly_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanFourierButterflyPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t pingpong0_index;
        uint32_t pingpong1_index;
        uint32_t butterfly_index;
        uint32_t stage;
        uint32_t direction;
        uint32_t pingpong;
    };
    PushConstants pc{};

    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.pingpong0_index =
            r.get_bindless_index(r.make_texture(r.fftocean.fourier_amplitudes_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.pingpong1_index = r.get_bindless_index(r.make_texture(r.fftocean.pingpong_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.butterfly_index = r.get_bindless_index(r.make_texture(r.fftocean.butterfly_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.amplitudes_image).vk_info.extent.width / 8u;
    uint32_t stages = r.get_image(r.fftocean.butterfly_image).vk_info.extent.width;
    VkImageMemoryBarrier2 barriers[]{
        Vks(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = r.get_image(r.fftocean.fourier_amplitudes_image).image,
            .subresourceRange = VkImageSubresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1u, .layerCount = 1u } }),
        Vks(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = r.get_image(r.fftocean.pingpong_image).image,
            .subresourceRange = VkImageSubresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1u, .layerCount = 1u } })
    };
    const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers });
    for(auto dir = 0u; dir < 2u; ++dir) {
        pc.direction = dir;
        for(auto stage = 0u; stage < stages; ++stage) {
            pc.stage = stage;
            pc.pingpong = pc.pingpong++ % 2;
            vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
            vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }
    r.fftocean.pingpong = pc.pingpong;
}

FFTOceanDisplacementPass::FFTOceanDisplacementPass(RenderGraph* rg)
    : RenderPass("FFTOceanDisplacementPass", PipelineSettings{ .shaders = { "fftocean/displacement.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();

    if(!r->fftocean.displacement_image) {
        r->fftocean.displacement_image =
            r->make_image("fftocean/displacement_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.fourier_amplitudes_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.pingpong_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.displacement_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanDisplacementPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t pingpong0_index;
        uint32_t pingpong1_index;
        uint32_t displacement_index;
        uint32_t pingpong;
    };
    PushConstants pc{};
    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.pingpong0_index =
            r.get_bindless_index(r.make_texture(r.fftocean.fourier_amplitudes_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.pingpong1_index = r.get_bindless_index(r.make_texture(r.fftocean.pingpong_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.displacement_index =
            r.get_bindless_index(r.make_texture(r.fftocean.displacement_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.pingpong = r.fftocean.pingpong;
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.displacement_image).vk_info.extent.width / 8u;
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanDebugGenH0Pass::FFTOceanDebugGenH0Pass(RenderGraph* rg)
    : RenderPass("FFTOceanDebugGenH0Pass", PipelineSettings{ .shaders = { "fftocean/debug_gen_h0.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();

    if(!r->fftocean.debug_h0_image) {
        r->fftocean.debug_h0_image =
            r->make_image("fftocean/debug_h0_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
        r->fftocean.debug_htx_image =
            r->make_image("fftocean/debug_htx_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
        r->fftocean.debug_htz_image =
            r->make_image("fftocean/debug_htz_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.gaussian_distribution_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_h0_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanDebugGenH0Pass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    if(!r.fftocean.recalc_state_0) { return; }
    r.fftocean.recalc_state_0 = false;

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t h0_index;
        uint32_t normal_dis_index;
    };
    PushConstants pc{};
    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.h0_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_h0_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.normal_dis_index =
            r.get_bindless_index(r.make_texture(r.fftocean.gaussian_distribution_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.debug_h0_image).vk_info.extent.width / 8u;
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanDebugGenHtPass::FFTOceanDebugGenHtPass(RenderGraph* rg)
    : RenderPass("FFTOceanDebugGenHtPass", PipelineSettings{ .shaders = { "fftocean/debug_gen_ht.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();

    if(!r->fftocean.displacement_image) {
        // r->fftocean.debug_ht_image =
        //     r->make_image("fftocean/debug_ht_image",
        //                   Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
        //                                          .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        //                                          .extent = { (uint32_t)r->fftocean.settings.num_samples,
        //                                                      (uint32_t)r->fftocean.settings.num_samples, 1u },
        //                                          .mipLevels = 1u,
        //                                          .arrayLayers = 1u,
        //                                          .samples = VK_SAMPLE_COUNT_1_BIT,
        //                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT }));
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_h0_image; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_htx_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_htz_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanDebugGenHtPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t h0_index;
        uint32_t hx_index;
        uint32_t hz_index;
        float time;
    };
    PushConstants pc{};
    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.h0_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_h0_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hx_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_htx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hz_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_htz_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.time = (float)glfwGetTime() * r.fftocean.settings.time_speed;
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.debug_h0_image).vk_info.extent.width / 8u;
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanDebugGenHxPass::FFTOceanDebugGenHxPass(RenderGraph* rg)
    : RenderPass("FFTOceanDebugGenHxPass", PipelineSettings{ .shaders = { "fftocean/debug_gen_hx.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();

    if(!r->fftocean.debug_hx_image) {
        r->fftocean.debug_hx_image =
            r->make_image("fftocean/debug_hx_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }));
        r->fftocean.debug_hxx_image =
            r->make_image("fftocean/debug_hxx_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }));
        r->fftocean.debug_hxz_image =
            r->make_image("fftocean/debug_hxz_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }));
        r->fftocean.debug_buffer =
            r->make_buffer("fftocean/debug_buffer",
                           Vks(VkBufferCreateInfo{ .size = 128, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT }),
                           VmaAllocationCreateInfo{});
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_h0_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_htx_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_htz_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hx_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxx_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxz_image; }),
                         .flags = AccessFlags::FROM_UNDEFINED_LAYOUT_BIT | AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanDebugGenHxPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t source;
        uint32_t destination;
        uint32_t buffer_index;
    };
    PushConstants pc{};
    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.source = r.get_bindless_index(r.make_texture(r.fftocean.debug_h0_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.destination = r.get_bindless_index(r.make_texture(r.fftocean.debug_hx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.buffer_index = r.get_bindless_index(r.fftocean.debug_buffer);
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.debug_h0_image).vk_info.extent.width / 8u;

    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);

    pc.source = r.get_bindless_index(r.make_texture(r.fftocean.debug_htx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
    pc.destination = r.get_bindless_index(r.make_texture(r.fftocean.debug_hxx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);

    pc.source = r.get_bindless_index(r.make_texture(r.fftocean.debug_htz_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
    pc.destination = r.get_bindless_index(r.make_texture(r.fftocean.debug_hxz_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanCalcNormalPass::FFTOceanCalcNormalPass(RenderGraph* rg)
    : RenderPass("FFTOceanCalcNormalPass", PipelineSettings{ .shaders = { "fftocean/debug_gen_normal.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();

    if(!r->fftocean.debug_hn_image) {
        r->fftocean.debug_hn_image =
            r->make_image("fftocean/debug_hn_image",
                          Vks(VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                 .extent = { (uint32_t)r->fftocean.settings.num_samples,
                                                             (uint32_t)r->fftocean.settings.num_samples, 1u },
                                                 .mipLevels = 1u,
                                                 .arrayLayers = 1u,
                                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }));
    }

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hn_image; }),
                         .flags = AccessFlags::WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_buffer; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hx_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxx_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxz_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanCalcNormalPass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t hy_index;
        uint32_t hx_index;
        uint32_t hz_index;
        uint32_t hn_index;
        uint32_t debug_index;
    };
    PushConstants pc{};
    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.hy_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hx_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hxx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hz_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hxz_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hn_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hn_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.debug_index = r.get_bindless_index(r.fftocean.debug_buffer);
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.debug_h0_image).vk_info.extent.width / 8u;
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

FFTOceanNormalizePass::FFTOceanNormalizePass(RenderGraph* rg)
    : RenderPass("FFTOceanNormalizePass", PipelineSettings{ .shaders = { "fftocean/debug_gen_normalize.comp.glsl" } }) {
    auto r = RendererVulkan::get_instance();

    accesses = { Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_buffer; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hx_image; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxx_image; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxz_image; }),
                         .flags = AccessFlags::READ_WRITE_BIT,
                         .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL } };
}

void FFTOceanNormalizePass::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();

    struct PushConstants {
        FFTOcean::FFTOceanSettings settings;
        uint32_t hy_index;
        uint32_t hx_index;
        uint32_t hz_index;
        uint32_t debug_index;
    };
    PushConstants pc{};
    static_assert(sizeof(pc) < 128);
    {
        pc.settings = r.fftocean.settings;
        pc.hy_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hx_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hxx_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.hz_index = r.get_bindless_index(r.make_texture(r.fftocean.debug_hxz_image, VK_IMAGE_LAYOUT_GENERAL, nullptr));
        pc.debug_index = r.get_bindless_index(r.fftocean.debug_buffer);
    }
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    uint32_t dispatch_size = r.get_image(r.fftocean.debug_h0_image).vk_info.extent.width / 8u;
    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0u, sizeof(pc), &pc);
    vkCmdDispatch(cmd, dispatch_size, dispatch_size, 1u);
}

ZPrepassPass::ZPrepassPass(RenderGraph* rg)
    : RenderPass("ZPrepassPass",
                 PipelineSettings{
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
    : RenderPass("VsmClearPagesPass", PipelineSettings{ .shaders = { "vsm/clear_page.comp.glsl" } }) {
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
    : RenderPass("VsmPageAllocPass", PipelineSettings{ .shaders = { "vsm/page_alloc.comp.glsl" } }) {
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
                 PipelineSettings{ .settings = RasterizationSettings{ .num_col_formats = 0, .depth_test = false, .depth_write = false },
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
    r.bindless_pool->bind(cmd, pipeline->bind_point);
    for(int i = 0; i < VSM_NUM_CLIPMAPS; ++i) {
        set_pc_vsm_shadows(cmd, VSM_NUM_CLIPMAPS - i - 1);
        vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                      sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer,
                                      0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    }
    vkCmdEndRendering(cmd);
}

VsmDebugPageCopyPass::VsmDebugPageCopyPass(RenderGraph* rg)
    : RenderPass("VsmDebugPageCopyPass", PipelineSettings{ .shaders = { "vsm/debug_page_alloc_copy.comp.glsl" } }) {
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
                 PipelineSettings{ .settings = RasterizationSettings{ .culling = VK_CULL_MODE_NONE,
                                                                      .depth_test = false,
                                                                      .depth_write = false,
                                                                      .depth_op = VK_COMPARE_OP_EQUAL },
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
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hx_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxx_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hxz_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         .layout = VK_IMAGE_LAYOUT_GENERAL },
                 Access{ .resource = rg->make_resource([r] { return r->fftocean.debug_hn_image; }),
                         .flags = AccessFlags::READ_BIT,
                         .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
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
    // ImGui::SetCurrentContext(Engine::get().ui_ctx->imgui_ctx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    Engine::get().ui->update();
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

} // namespace gfx