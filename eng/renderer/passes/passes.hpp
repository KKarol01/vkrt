#pragma once

#include <random>
#include <type_traits>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan/vulkan_backend.hpp> // todo: remove this
#include <eng/engine.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/renderer/vulkan/vulkan_structs.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/common/hash.hpp>

#include <vulkan/vulkan.h>

namespace eng
{

namespace gfx
{

namespace pass
{

struct PassSettings
{
    using Name = StackString<32>;
    using Value = std::variant<float*>;
    // takes string_view and ref to variant with types
    void iterate_settings(const auto& cb)
        requires(std::is_invocable_r_v<bool, decltype(cb), std::string_view, Value&>)
    {
        for(auto& [name, value] : settings)
        {
            modified |= cb(name.as_view(), value);
        }
    }
    std::vector<std::pair<Name, Value>> settings;
    bool modified{};
};

struct Pass
{
    Pass(std::string_view name, uint32_t order) : name(name), order(order) {}
    virtual ~Pass() = default;
    virtual void init(RGRenderGraph* graph) = 0;
    std::string name;
    uint32_t order{};
    PassSettings settings;
};

struct ZPrepass : public Pass
{
    struct PassData
    {
        RGAccessId constants;
        RGAccessId instances;
        RGAccessId positions;
        RGAccessId indirect;

        RGResourceId out_depth;
    };

    ZPrepass(RenderPassType type) : Pass("MESH_PASS_Z_PREPASS", RenderOrder::Z_PREPASS), type(type)
    {
        auto& r = get_renderer();
        pipeline = r.settings.default_z_prepass_pipeline;
    }

    ~ZPrepass() override = default;

    void init(RGRenderGraph* graph) override
    {
        if(!graph || !pipeline) { return; }
        graph->add_graphics_pass<PassData>(
            name.data(), order,
            [this](RGBuilder& b, PassData& d) {
                const auto& r = get_renderer();
                const auto& cd = *r.current_data;
                const auto& settings = r.settings;
                const auto& md = r.mesh_render_data[(int)type];
                const auto resolution = settings.render_resolution;
                auto depacc = b.create_resource(ENG_FMT("{}_DEPTH", name),
                                                Image::init(resolution.x, resolution.y, settings.depth_format,
                                                            ImageUsage::DEPTH_BIT | ImageUsage::SAMPLED_BIT),
                                                RGClear::depth_stencil(0.0, {}));
                depacc = b.access_depth(depacc, ImageFormat::D32_SFLOAT);

                d.constants = b.as_acc_id(cd.render_resources.constants);
                d.constants = b.read_buffer(d.constants);
                // todo: add access_indirect
                d.indirect = b.import_resource(md.draw.indirect_buf);
                d.indirect = b.access_resource(d.indirect, PipelineStage::INDIRECT_BIT, PipelineAccess::INDIRECT_READ_BIT);
                d.instances = b.import_resource(md.instance_buffer);
                d.instances = b.read_buffer(d.instances);
                d.positions = b.import_resource(r.bufs.positions);
                d.positions = b.read_buffer(d.positions);

                d.out_depth = b.as_res_id(depacc);
                r.current_data->render_resources.zpdepth = d.out_depth;
            },
            [this](RGBuilder& b, const PassData& d) {
                const auto& r = get_renderer();
                const auto& cd = *r.current_data;
                const auto& settings = r.settings;
                const auto render_res = settings.render_resolution;
                const auto& md = r.mesh_render_data[(int)type];

                auto vkdep = vk::VkRenderingAttachmentInfo{};
                vkdep.imageView = b.graph->get_acc(d.out_depth).image_view.get_md().vk->view;
                vkdep.imageLayout = to_vk(b.graph->get_acc(d.out_depth).layout);
                vkdep.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                vkdep.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                auto vkrinfo = vk::VkRenderingInfo{};
                vkrinfo.renderArea = { .offset = {}, .extent = { (uint32_t)render_res.x, (uint32_t)render_res.y } };
                vkrinfo.layerCount = 1;
                vkrinfo.pDepthAttachment = &vkdep;
                VkViewport viewport{ 0.0, 0.0, render_res.x, render_res.y, 0.0, 1.0 };
                VkRect2D scissor{ {}, { (uint32_t)render_res.x, (uint32_t)render_res.y } };
                DescriptorResource shaderresources[]{ DescriptorResource::storage_buffer(b.graph->get_buf(d.constants)),
                                                      DescriptorResource::storage_buffer(b.graph->get_buf(d.positions)) };
                auto* cmd = b.open_cmd_buf();
                cmd->begin_rendering(vkrinfo);
                cmd->set_viewports(&viewport, 1);
                cmd->set_scissors(&scissor, 1);
                cmd->bind_resources(0, shaderresources);
                md.draw.draw([&](const IndirectDrawParams& params) {
                    cmd->bind_pipeline(params.draw->pipeline.get());
                    cmd->bind_index(get_renderer().bufs.indices.get(), 0ull, VK_INDEX_TYPE_UINT16);
                    cmd->draw_indexed_indirect_count(md.draw.cmds_view.buffer.get(), params.command_offset_bytes,
                                                     md.draw.counts_view.buffer.get(), params.count_offset_bytes,
                                                     params.max_draw_count, params.stride);
                });
                cmd->end_rendering();
            });
    }

    RenderPassType type{ RenderPassType::LAST_ENUM };
    Handle<Pipeline> pipeline;
    PassData data;
};

struct NormalFromDepth : public Pass
{
    struct PassData
    {
        RGAccessId constants;
        RGAccessId depth;
        RGResourceId out_normals;
    };

    NormalFromDepth() : Pass("DEPTH_NORMAL", RenderOrder::POST_Z)
    {
        auto& r = get_renderer();
        pipeline =
            r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/normal_reconstruction/normal.cs.hlsl" }));
    }

    ~NormalFromDepth() override = default;

    void init(RGRenderGraph* graph) override
    {
        if(!graph) { return; }
        data = graph->add_compute_pass<PassData>(
            name.data(), order,
            [this](RGBuilder& b, PassData& d) {
                const auto& r = get_renderer();
                const auto res = r.settings.render_resolution;
                if(!r.current_data->render_resources.zpdepth) { return; }
                d.constants = b.read_buffer(b.as_acc_id(r.current_data->render_resources.constants));
                d.depth = b.sample_texture(b.as_acc_id(r.current_data->render_resources.zpdepth), ImageFormat::D32_SFLOAT);
                auto out_normals = b.create_resource(ENG_FMT("{}_NORMAL", name),
                                                     Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                                 ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT));
                out_normals = b.read_write_image(out_normals);
                d.out_normals = b.as_res_id(out_normals);
                r.current_data->render_resources.normal = d.out_normals;
            },
            [this](RGBuilder& b, const PassData& d) {
                if(!d.out_normals) { return; }
                const auto& r = get_renderer();
                const auto& normals_img = b.graph->get_img(b.as_acc_id(d.out_normals));
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(pipeline.get());

                DescriptorResource resources[]{
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.constants).buffer_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.depth).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(b.as_acc_id(d.out_normals)).image_view)
                };
                cmd->bind_resources(1, resources);
                cmd->dispatch((normals_img->width + 7) / 8, (normals_img->height + 7) / 8, 1);
            });
    }

    Handle<Pipeline> pipeline;
    PassData data;
};

struct SSAO : public Pass
{
    struct PassData
    {
        RGAccessId constants;
        RGAccessId settings;
        RGAccessId depth;
        RGAccessId normals;
        RGAccessId samples;
        RGAccessId noise;
        RGResourceId out_ao;
    };

    SSAO() : Pass("SSAO", RenderOrder::POST_Z)
    {
        auto& r = get_renderer();
        ao_pipeline = r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/ssao/ssao.cs.hlsl" }));
        blur_pipeline = r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/ssao/blur.cs.hlsl" }));
        settings_buffer = r.make_buffer("SSAO_SETTINGS", Buffer::init(sizeof(GPUEngAOSettings),
                                                                      BufferUsage::STORAGE_BIT | BufferUsage::CPU_ACCESS));
        ao_settings = {
            .radius = 0.5f,
            .bias = 0.04f,
        };
        settings = {
            {
                { "radius", &ao_settings.radius },
                { "bias", &ao_settings.bias },
            },
            true,
        };
        generate_samples();
        generate_noise();
    }

    ~SSAO() override = default;

    void init(RGRenderGraph* graph) override
    {
        if(!graph) { return; }
        poll_settings_change();
        data = graph->add_compute_pass<PassData>(
            name.data(), order,
            [this](RGBuilder& b, PassData& d) {
                const auto& r = get_renderer();
                const auto res = r.settings.render_resolution;
                if(!r.current_data->render_resources.zpdepth) { return; }
                d.constants = b.read_buffer(b.as_acc_id(r.current_data->render_resources.constants));
                d.depth = b.sample_texture(b.as_acc_id(r.current_data->render_resources.zpdepth), ImageFormat::D32_SFLOAT);
                d.normals = b.read_image(b.as_acc_id(r.current_data->render_resources.normal));
                d.noise = b.sample_texture(b.import_resource(noise_texture));
                d.samples = b.read_buffer(b.import_resource(sample_buffer));
                auto out_ao = b.create_resource("SSAO_OUTPUT", Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                                           ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT));
                out_ao = b.read_write_image(out_ao);
                d.out_ao = b.as_res_id(out_ao);
                d.settings = b.import_resource(settings_buffer);
                d.settings = b.read_buffer(d.settings);
            },
            [this](RGBuilder& b, const PassData& d) {
                if(!d.out_ao) { return; }
                const auto& r = get_renderer();
                const auto& out_img = b.graph->get_img(b.as_acc_id(d.out_ao));
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(ao_pipeline.get());

                DescriptorResource resources[]{
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.constants).buffer_view),
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.settings).buffer_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.depth).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(d.normals).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(b.as_acc_id(d.out_ao)).image_view),
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.samples).buffer_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.noise).image_view)
                };
                cmd->bind_resources(1, resources);
                cmd->dispatch((out_img->width + 7) / 8, (out_img->height + 7) / 8, 1);
            });

        struct BlurData
        {
            RGAccessId in_ao;
            RGResourceId out_blur;
        };

        const auto blurdata = graph->add_compute_pass<BlurData>(
            "SSAO_BLUR", order,
            [this](RGBuilder& b, BlurData& d) {
                const auto& r = get_renderer();
                const auto res = r.settings.render_resolution;
                d.in_ao = b.read_write_image(b.as_acc_id(data.out_ao));
                d.out_blur = b.as_res_id(b.write_image(
                    b.create_resource("SSAO_BLUR", Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                               ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT))));
                data.out_ao = d.out_blur;
            },
            [this](RGBuilder& b, const BlurData& d) {
                if(!blur_pipeline) { return; }
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(blur_pipeline.get());
                const auto img = b.graph->get_img(d.in_ao);
                DescriptorResource resources[]{
                    DescriptorResource::storage_image(b.graph->get_acc(d.in_ao).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(d.out_blur).image_view),
                };
                cmd->bind_resources(1, resources);
                cmd->dispatch((img->width + 7) / 8, (img->height + 7) / 8, 1);
            });

        const auto& r = get_renderer();
        r.current_data->render_resources.ao = data.out_ao;
    }

    void generate_samples()
    {
        glm::vec3 samples[64];
        std::default_random_engine eng(0);
        std::uniform_real_distribution<float> dist(0.0, 1.0);
        const auto lerp = [](auto a, auto b, auto t) { return a + (b - a) * t; };
        for(auto i = 0u; i < std::size(samples); ++i)
        {
            samples[i] = glm::vec3{ dist(eng) * 2.0 - 1.0, dist(eng) * 2.0 - 1.0, dist(eng) };
            samples[i] = glm::normalize(samples[i]);
            auto scale = (float)i / std::size(samples);
            scale = lerp(0.1f, 1.0f, scale * scale);
            samples[i] = samples[i] * scale;
        }
        if(!sample_buffer)
        {
            sample_buffer =
                get_renderer().make_buffer("SSAO_KERNELS",
                                           Buffer::init(sizeof(samples), BufferUsage::STORAGE_BIT | BufferUsage::CPU_ACCESS));
        }
        get_renderer().staging->copy(sample_buffer.get(), samples, 0ull, sizeof(samples));
    }

    void generate_noise()
    {
        glm::vec4 noise[16];
        std::default_random_engine eng(133543);
        std::uniform_real_distribution<float> dist(0.0, 1.0);
        for(auto i = 0u; i < std::size(noise); ++i)
        {
            glm::vec3 n{ dist(eng) * 2.0f - 1.0f, dist(eng) * 2.0f - 1.0f, 0.0f };
            noise[i] = glm::vec4{ n.x, n.y, n.z, 0.0 };
        }
        if(!noise_texture)
        {
            noise_texture = get_renderer().make_image("SSAO_NOISE", Image::init(4, 4, ImageFormat::R16FG16FB16FA16F,
                                                                                ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_DST_BIT,
                                                                                ImageLayout::READ_ONLY));
        }
        get_renderer().staging->copy(noise_texture.get(), noise, 0, 0);
        get_renderer().staging->get_wait_sem()->wait_cpu(~0ull);
    }

    void poll_settings_change()
    {
        if(settings.modified)
        {
            auto& r = get_renderer();
            r.staging->copy(settings_buffer.get(), &ao_settings, 0ull, sizeof(ao_settings));
            settings.modified = false;
        }
    }

    GPUEngAOSettings ao_settings;
    Handle<Pipeline> ao_pipeline;
    Handle<Pipeline> blur_pipeline;
    Handle<Buffer> sample_buffer;
    Handle<Image> noise_texture;
    Handle<Buffer> settings_buffer;
    PassData data;
};

struct GTAO : public Pass
{
    struct PassData
    {
        RGAccessId constants;
        RGAccessId settings;
        RGAccessId depth;
        RGAccessId normals;
        RGAccessId samples;
        RGAccessId noise;
        RGResourceId out_ao;
    };

    GTAO() : Pass("GTAO", RenderOrder::POST_Z)
    {
        auto& r = get_renderer();
        ao_pipeline = r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/gtao/gtao.cs.hlsl" }));
        blur_pipeline = r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/ssao/blur.cs.hlsl" }));
        settings_buffer = r.make_buffer("GTAO_SETTINGS", Buffer::init(sizeof(GPUEngAOSettings),
                                                                      BufferUsage::STORAGE_BIT | BufferUsage::CPU_ACCESS));
        ao_settings = {
            .radius = 0.006f,
            .bias = 0.0f,
        };
        settings = {
            {
                { "radius", &ao_settings.radius },
                { "bias", &ao_settings.bias },
            },
            true,
        };
        generate_noise();
    }

    ~GTAO() override = default;

    void init(RGRenderGraph* graph) override
    {
        if(!graph) { return; }
        poll_settings_change();
        data = graph->add_compute_pass<PassData>(
            name.data(), order,
            [this](RGBuilder& b, PassData& d) {
                const auto& r = get_renderer();
                const auto res = r.settings.render_resolution;
                if(!r.current_data->render_resources.zpdepth) { return; }
                d.constants = b.read_buffer(b.as_acc_id(r.current_data->render_resources.constants));
                d.depth = b.sample_texture(b.as_acc_id(r.current_data->render_resources.zpdepth), ImageFormat::D32_SFLOAT);
                d.normals = b.read_image(b.as_acc_id(r.current_data->render_resources.normal));
                d.noise = b.sample_texture(b.import_resource(noise_texture));
                auto out_ao = b.create_resource("GTAO_OUTPUT", Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                                           ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT));
                out_ao = b.read_write_image(out_ao);
                d.out_ao = b.as_res_id(out_ao);
                d.settings = b.import_resource(settings_buffer);
                d.settings = b.read_buffer(d.settings);
            },
            [this](RGBuilder& b, const PassData& d) {
                if(!d.out_ao) { return; }
                const auto& r = get_renderer();
                const auto& out_img = b.graph->get_img(b.as_acc_id(d.out_ao));
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(ao_pipeline.get());

                DescriptorResource resources[]{
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.constants).buffer_view),
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.settings).buffer_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.depth).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(d.normals).image_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.noise).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(b.as_acc_id(d.out_ao)).image_view),
                };
                cmd->bind_resources(1, resources);
                cmd->dispatch((out_img->width + 7) / 8, (out_img->height + 7) / 8, 1);
            });

        struct BlurData
        {
            RGAccessId in_ao;
            RGResourceId out_blur;
        };

        const auto blurdata = graph->add_compute_pass<BlurData>(
            "GTAO_BLUR", order,
            [this](RGBuilder& b, BlurData& d) {
                const auto& r = get_renderer();
                const auto res = r.settings.render_resolution;
                d.in_ao = b.read_write_image(b.as_acc_id(data.out_ao));
                d.out_blur = b.as_res_id(b.write_image(
                    b.create_resource("GTAO_BLUR", Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                               ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT))));
                data.out_ao = d.out_blur;
            },
            [this](RGBuilder& b, const BlurData& d) {
                if(!blur_pipeline) { return; }
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(blur_pipeline.get());
                const auto img = b.graph->get_img(d.in_ao);
                DescriptorResource resources[]{
                    DescriptorResource::storage_image(b.graph->get_acc(d.in_ao).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(d.out_blur).image_view),
                };
                cmd->bind_resources(1, resources);
                cmd->dispatch((img->width + 7) / 8, (img->height + 7) / 8, 1);
            });

        const auto& r = get_renderer();
        r.current_data->render_resources.ao = data.out_ao;
    }

    void generate_noise()
    {
        glm::vec4 noise[16];
        std::default_random_engine eng(133543);
        std::uniform_real_distribution<float> dist(0.0, 1.0);
        for(auto i = 0u; i < std::size(noise); ++i)
        {
            glm::vec3 n{ dist(eng) * 2.0f - 1.0f, dist(eng) * 2.0f - 1.0f, 0.0f };
            noise[i] = glm::vec4{ n.x, n.y, n.z, 0.0 };
        }
        if(!noise_texture)
        {
            noise_texture = get_renderer().make_image("GTAO_NOISE", Image::init(4, 4, ImageFormat::R16FG16FB16FA16F,
                                                                                ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_DST_BIT,
                                                                                ImageLayout::READ_ONLY));
        }
        get_renderer().staging->copy(noise_texture.get(), noise, 0, 0);
        // get_renderer().staging->flush()->wait_cpu(~0ull);
    }

    void poll_settings_change()
    {
        if(settings.modified)
        {
            auto& r = get_renderer();
            r.staging->copy(settings_buffer.get(), &ao_settings, 0ull, sizeof(ao_settings));
            settings.modified = false;
        }
    }

    GPUEngAOSettings ao_settings;
    Handle<Pipeline> ao_pipeline;
    Handle<Pipeline> blur_pipeline;
    Handle<Buffer> sample_buffer;
    Handle<Image> noise_texture;
    Handle<Buffer> settings_buffer;
    PassData data;
};

struct RTAO : public Pass
{
    RTAO() : Pass("RTAO", RenderOrder::MESH_RENDER)
    {
        auto& r = get_renderer();
        pipeline = r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/rtao/rtao.cs.hlsl" }));
        settings_buffer = r.make_buffer("GTAO_SETTINGS", Buffer::init(sizeof(GPUEngAOSettings),
                                                                      BufferUsage::STORAGE_BIT | BufferUsage::CPU_ACCESS));
        blur_pipeline = r.make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/ssao/blur.cs.hlsl" }));
        ao_settings = {
            .radius = 0.006f,
            .bias = 0.0f,
        };
        settings = {
            {
                { "radius", &ao_settings.radius },
                { "bias", &ao_settings.bias },
            },
            true,
        };
    }
    ~RTAO() override = default;

    struct PassData
    {
        RGAccessId constants;
        RGAccessId depth;
        RGAccessId normals;
        RGResourceId out_ao;
        RGAccessId settings;
    };

    void init(RGRenderGraph* graph) override
    {
        const auto& r = get_renderer();
        const auto res = r.settings.render_resolution;
        poll_settings_change();
        data = graph->add_compute_pass<PassData>(
            name.c_str(), order,
            [=](RGBuilder& b, PassData& d) {
                if(!r.current_data->render_resources.zpdepth) { return; }
                d.constants = b.read_buffer(b.as_acc_id(r.current_data->render_resources.constants));
                d.depth = b.sample_texture(b.as_acc_id(r.current_data->render_resources.zpdepth), ImageFormat::D32_SFLOAT);
                d.normals = b.read_image(b.as_acc_id(r.current_data->render_resources.normal));
                auto out_ao = b.create_resource("RTAO_OUTPUT", Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                                           ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT));
                out_ao = b.read_write_image(out_ao);
                d.out_ao = b.as_res_id(out_ao);
                d.settings = b.import_resource(settings_buffer);
                d.settings = b.read_buffer(d.settings);
            },
            [=, this](RGBuilder& b, const PassData& d) {
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(pipeline.get());
                DescriptorResource resources[]{
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.constants).buffer_view),
                    DescriptorResource::storage_buffer(b.graph->get_acc(d.settings).buffer_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.depth).image_view),
                    DescriptorResource::sampled_image(b.graph->get_acc(d.normals).image_view),
                    DescriptorResource::acceleration_struct(get_renderer().bufs.geom_tlas),
                    DescriptorResource::storage_image(b.graph->get_acc(d.out_ao).image_view),
                };
                cmd->bind_resources(0, resources);
                cmd->dispatch((res.x + 7) / 8, (res.y + 7) / 8, 1);
            });

        struct BlurData
        {
            RGAccessId in_ao;
            RGResourceId out_blur;
        };

        const auto blurdata = graph->add_compute_pass<BlurData>(
            "GTAO_BLUR", order,
            [this](RGBuilder& b, BlurData& d) {
                const auto& r = get_renderer();
                const auto res = r.settings.render_resolution;
                d.in_ao = b.read_write_image(b.as_acc_id(data.out_ao));
                d.out_blur = b.as_res_id(b.write_image(
                    b.create_resource("GTAO_BLUR", Image::init(res.x, res.y, ImageFormat::R16FG16FB16FA16F,
                                                               ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT))));
                data.out_ao = d.out_blur;
            },
            [this](RGBuilder& b, const BlurData& d) {
                if(!blur_pipeline) { return; }
                auto* cmd = b.open_cmd_buf();
                cmd->bind_pipeline(blur_pipeline.get());
                const auto img = b.graph->get_img(d.in_ao);
                DescriptorResource resources[]{
                    DescriptorResource::storage_image(b.graph->get_acc(d.in_ao).image_view),
                    DescriptorResource::storage_image(b.graph->get_acc(d.out_blur).image_view),
                };
                cmd->bind_resources(1, resources);
                cmd->dispatch((img->width + 7) / 8, (img->height + 7) / 8, 1);
            });

        r.current_data->render_resources.ao = data.out_ao;
    }

    void poll_settings_change()
    {
        if(settings.modified)
        {
            auto& r = get_renderer();
            r.staging->copy(settings_buffer.get(), &ao_settings, 0ull, sizeof(ao_settings));
            settings.modified = false;
        }
    }

    GPUEngAOSettings ao_settings;
    Handle<Pipeline> pipeline;
    Handle<Pipeline> blur_pipeline;
    Handle<Buffer> settings_buffer;
    PassData data;
};

struct MeshPass : public Pass
{
    struct PassData
    {
        RGAccessId constants;
        RGAccessId instances;
        RGAccessId positions;
        RGAccessId indirect;
        RGAccessId depth;
        RGAccessId out_color;
    };

    MeshPass(RenderPassType type) : Pass(ENG_FMT("MESH_PASS_{}", to_string(type)), RenderOrder::MESH_RENDER), type(type)
    {
    }

    ~MeshPass() override = default;

    void init(RGRenderGraph* graph) override
    {
        if(!graph) { return; }
        data = graph->add_graphics_pass<PassData>(
            name.data(), order,
            [this](RGBuilder& b, PassData& d) {
                const auto& r = get_renderer();
                const auto& cd = *r.current_data;
                const auto& settings = r.settings;
                const auto& md = r.mesh_render_data[(int)type];
                if(md.built_instances.empty()) { return; }
                const auto resolution = settings.render_resolution;
                const auto color_usage = ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT;
                d.out_color = b.create_resource(ENG_FMT("{}_COLOR", name),
                                                Image::init(resolution.x, resolution.y, settings.color_format, color_usage),
                                                RGClear::color());
                auto depacc = b.as_acc_id(cd.render_resources.zpdepth);
                d.out_color = b.access_color(d.out_color);
                depacc = b.access_depth(depacc);

                d.constants = b.as_acc_id(cd.render_resources.constants);
                d.constants = b.read_buffer(d.constants);
                // todo: add access_indirect
                d.indirect = b.import_resource(md.draw.indirect_buf);
                d.indirect = b.access_resource(d.indirect, PipelineStage::INDIRECT_BIT, PipelineAccess::INDIRECT_READ_BIT);
                d.instances = b.import_resource(md.instance_buffer);
                d.instances = b.read_buffer(d.instances);
                d.positions = b.import_resource(r.bufs.positions);
                d.positions = b.read_buffer(d.positions);
                d.depth = depacc;

                r.current_data->render_resources.color = b.as_res_id(d.out_color);
            },
            [this](RGBuilder& b, const PassData& d) {
                if(!d.out_color) { return; }

                const auto& r = get_renderer();
                const auto& cd = *r.current_data;
                const auto& settings = r.settings;
                const auto render_res = settings.render_resolution;
                const auto& md = r.mesh_render_data[(int)type];

                vk::VkRenderingAttachmentInfo vkcols[1]{};
                vkcols[0].imageView = b.graph->get_acc(d.out_color).image_view.get_md().vk->view;
                vkcols[0].imageLayout = to_vk(b.graph->get_acc(d.out_color).layout);
                vkcols[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                vkcols[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                auto vkdep = vk::VkRenderingAttachmentInfo{};
                vkdep.imageView = b.graph->get_acc(d.depth).image_view.get_md().vk->view;
                vkdep.imageLayout = to_vk(b.graph->get_acc(d.depth).layout);
                vkdep.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                vkdep.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

                auto vkrinfo = vk::VkRenderingInfo{};
                vkrinfo.renderArea = { .offset = {}, .extent = { (uint32_t)render_res.x, (uint32_t)render_res.y } };
                vkrinfo.layerCount = 1;
                vkrinfo.colorAttachmentCount = 1;
                vkrinfo.pColorAttachments = vkcols;
                vkrinfo.pDepthAttachment = &vkdep;
                VkViewport viewport{ 0.0, 0.0, render_res.x, render_res.y, 0.0, 1.0 };
                VkRect2D scissor{ {}, { (uint32_t)render_res.x, (uint32_t)render_res.y } };
                DescriptorResource shaderresources[]{
                    DescriptorResource::storage_buffer(b.graph->get_buf(d.constants)),
                    DescriptorResource::storage_buffer(b.graph->get_buf(d.positions)),
                };
                auto* cmd = b.open_cmd_buf();
                cmd->begin_rendering(vkrinfo);
                cmd->set_viewports(&viewport, 1);
                cmd->set_scissors(&scissor, 1);
                cmd->bind_resources(0, shaderresources);
                md.draw.draw([&](const IndirectDrawParams& params) {
                    cmd->bind_pipeline(params.draw->pipeline.get());
                    cmd->bind_index(get_renderer().bufs.indices.get(), 0ull, VK_INDEX_TYPE_UINT16);
                    cmd->draw_indexed_indirect_count(md.draw.cmds_view.buffer.get(), params.command_offset_bytes,
                                                     md.draw.counts_view.buffer.get(), params.count_offset_bytes,
                                                     params.max_draw_count, params.stride);
                });
                cmd->end_rendering();
            });
    }

    RenderPassType type{ RenderPassType::LAST_ENUM };
    PassData data;
};

namespace culling
{

// class ZPrepass
//{
//   public:
//     void init(RenderGraph* rg)
//     {
//
//         zbufs = info.zbufs;
//         culled_id_bufs = rg->make_resource(BufferDescriptor{
//             "cull ids", 2 * 1024 * 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT | BufferUsage::CPU_ACCESS });
//         culled_cmd_bufs =
//             rg->make_resource(BufferDescriptor{ "cull cmds", 2 * 1024 * 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT },
//                               r->frame_count);
//         cullzout_pipeline = r->make_pipeline(PipelineCreateInfo{
//             .shaders = { r->make_shader("common/zoutput.vert.glsl"), r->make_shader("common/zoutput.frag.glsl") },
//             .layout = r->bindless_pplayout,
//             .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
//             .depth_test = true,
//             .depth_write = true,
//             .depth_compare = DepthCompare::GREATER,
//             .culling = CullFace::BACK,
//         });
//     }
//     void setup(RenderGraph* rg)
//     {
//         auto& r = get_renderer();
//         auto* w = get_engine().window;
//         auto& pf = r->get_perframe();
//
//         struct ZbufPass
//         {
//             RenderGraph::RAH zbuf;
//             RenderGraph::RAH ids;
//             RenderGraph::RAH ;
//         };
//
//         rg->add_compute_pass<ZbufPass>(
//             "zbuf",
//             [=](RenderGraph::PassBuilder& pb) {
//                 ZbufPass pass;
//                 pass.zbuf = b.import_resource(pf.gbuffer.depth, RenderGraph::Clear::depth_stencil({ 0.0f, 0u }));
//                 pass.zbuf = b.access_depth_attachment(pass.zbuf);
//                 return pass;
//             },
//             [](RenderGraph& rg, CommandBuffer* cmd) {
//
//             });
//
//         auto zbuf = rg->import_resource(pf.gbuffer.depth);
//         zbuf = rg->access_depth_attachment(zbuf);
//
//         access(zbufs, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW, ImageLayout::ATTACHMENT, true);
//         access(culled_id_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
//         access(culled_cmd_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
//         VkViewport vkview{ 0.0f, 0.0f, get_engine().window->width, get_engine().window->height, 0.0f, 1.0f };
//         VkRect2D vksciss{ {}, { (uint32_t)get_engine().window->width, (uint32_t)get_engine().window->height } };
//         const auto vkdep =
//             Vks(VkRenderingAttachmentInfo{ .imageView = rg->get_resource(zbufs).image->default_view->md.vk->view,
//                                            .imageLayout = to_vk(ImageLayout::ATTACHMENT),
//                                            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
//                                            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
//                                            .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0u } } });
//         const auto vkreninfo = Vks(VkRenderingInfo{ .renderArea = vksciss, .layerCount = 1, .pDepthAttachment = &vkdep });
//         cmd->set_scissors(&vksciss, 1);
//         cmd->set_viewports(&vkview, 1);
//         cmd->bind_index(r->bufs.idx_buf.get(), 0, r->bufs.index_type);
//         cmd->bind_pipeline(cullzout_pipeline.get());
//         cmd->bind_resource(0, r->get_perframe().constants);
//         cmd->bind_resource(1, rg->get_resource(culled_id_bufs.get(-1)).buffer);
//         cmd->begin_rendering(vkreninfo);
//         r->render_ibatch(cmd, (*ibatches)[r->get_perframe_index(-1)], nullptr, false);
//         cmd->end_rendering();
//     }
//     RenderGraph::RAH zbufs;
//     RenderGraph::RAH culled_id_bufs;
//     RenderGraph::RAH culled_cmd_bufs;
//     const std::vector<gfx::Renderer::IndirectBatch>* ibatches; // todo: this shouldn't be here
//     Handle<Pipeline> cullzout_pipeline;
// };
//
// class Hiz
//{
//   public:
//     void register_pass(RenderGraph& rg)
//     {
//         struct Ret1
//         {
//             RenderGraph::RAH zbuffer;
//         };
//         rg.add_graphics_pass<Ret1>(
//             "p1",
//             [&rg] {
//             Ret1 ret;
//             ret.zbuffer = rg.import_resource(image);
//             ret.zbuffer = rg.access_resource(ret.zbuffer, )
//
//                               .zbuffer =
//                 };
//     },
//             [] {});
// }
// };
//
// class Hiz : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         RenderGraph::ResourceView zbufs;
//     };
//     Hiz(RenderGraph* g, const CreateInfo& info) : Pass("culling::Hiz", RenderOrder::DEFAULT_UNLIT)
//     {
//         auto& r = get_renderer();
//         auto* w = get_engine().window;
//         zbuf = info.zbufs;
//         const auto hizpmips = (uint32_t)(std::log2f(std::max(w->width, w->height)) + 1);
//         hiz = g->make_resource(ImageDescriptor{ .name = "hizpyramid",
//                                                 .width = (uint32_t)w->width,
//                                                 .height = (uint32_t)w->height,
//                                                 .mips = (uint32_t)(hizpmips),
//                                                 .format = ImageFormat::R32F,
//                                                 .usage = ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT },
//                                r->frame_count);
//         hiz_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
//             .shaders = { get_engine().renderer->make_shader("culling/hiz.comp.glsl") }, .layout = r->bindless_pplayout });
//     }
//     void setup() override
//     {
//         access(zbuf, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
//         access(hiz, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, ImageLayout::GENERAL, true);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
//         auto& hizp = rg->get_resource(hiz).image.get();
//         cmd->bind_pipeline(hiz_pipeline.get());
//         cmd->bind_resource(4, r->make_texture(TextureDescriptor{ rg->get_resource(zbuf).image->default_view,
//                                                                  ImageLayout::GENERAL, false }));
//         cmd->bind_resource(5, r->make_texture(TextureDescriptor{
//                                   r->make_view(ImageViewDescriptor{ .image = rg->get_resource(hiz).image, .mips = { 0, 1 } }),
//                                   ImageLayout::GENERAL, true }));
//         cmd->dispatch((hizp.width + 31) / 32, (hizp.height + 31) / 32, 1);
//         cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
//         for(auto i = 1u; i < hizp.mips; ++i)
//         {
//             cmd->bind_resource(4, r->make_texture(TextureDescriptor{
//                                       r->make_view(ImageViewDescriptor{ .image = rg->get_resource(hiz).image, .mips = { i - 1, 1 } }),
//                                       ImageLayout::GENERAL, false }));
//             cmd->bind_resource(5, r->make_texture(TextureDescriptor{
//                                       r->make_view(ImageViewDescriptor{ .image = rg->get_resource(hiz).image, .mips = { i, 1 } }),
//                                       ImageLayout::GENERAL, true }));
//             const auto sx = ((hizp.width >> i) + 31) / 32;
//             const auto sy = ((hizp.height >> i) + 31) / 32;
//             cmd->dispatch(sx, sy, 1);
//             cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
//         }
//     }
//     RenderGraph::ResourceView zbuf;
//     RenderGraph::ResourceView hiz;
//     Handle<Pipeline> hiz_pipeline;
// };
//
// class MainPass : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         const gfx::Renderer::RenderPass* fwd;
//         const ZPrepass* pzprepass;
//         const Hiz* phiz;
//     };
//     MainPass(RenderGraph* g, const CreateInfo& info) : Pass("culling::MainPass", RenderOrder::DEFAULT_UNLIT)
//     {
//         auto& r = get_renderer();
//         fwd = info.fwd;
//         culled_id_bufs = info.pzprepass->culled_id_bufs;
//         culled_cmd_bufs = info.pzprepass->culled_cmd_bufs;
//         std::vector<Handle<Buffer>> vfwd_batch_ids(r->frame_count);
//         std::vector<Handle<Buffer>> vfwd_batch_cmds(r->frame_count);
//         batches.resize(r->frame_count);
//         for(auto i = 0u; i < r->frame_count; ++i)
//         {
//             vfwd_batch_ids[i] = fwd->batch.ids_buf;
//             vfwd_batch_cmds[i] = fwd->batch.cmd_buf;
//             batches[i].ids_buf = g->get_resource(culled_id_bufs.at(i)).buffer;
//             batches[i].cmd_buf = g->get_resource(culled_cmd_bufs.at(i)).buffer;
//         }
//         fwd_id_bufs = g->import_resource(std::span{ vfwd_batch_ids });
//         fwd_cmd_bufs = g->import_resource(std::span{ vfwd_batch_cmds });
//         hiz = info.phiz->hiz;
//         cull_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
//             .shaders = { get_engine().renderer->make_shader("culling/culling.comp.glsl") },
//             .layout = r->bindless_pplayout,
//         });
//     }
//     void setup() override
//     {
//         access(fwd_id_bufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT);
//         access(fwd_cmd_bufs, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
//         access(culled_id_bufs, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
//                PipelineAccess::SHADER_RW | PipelineAccess::TRANSFER_WRITE_BIT);
//         access(culled_cmd_bufs, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
//                PipelineAccess::SHADER_RW | PipelineAccess::TRANSFER_WRITE_BIT);
//         access(hiz, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
//         const auto pfi = r->get_perframe_index();
//         batches[pfi].batches = rp.batch.batches;
//         batches[pfi].cmd_count = rp.batch.cmd_count;
//         batches[pfi].cmd_start = rp.batch.cmd_start;
//         batches[pfi].ids_count = rp.batch.ids_count;
//
//         const auto ZERO = 0u;
//         r->sbuf->resize(rg->get_resource(culled_id_bufs).buffer, rp.batch.ids_buf->size);
//         r->sbuf->copy(rg->get_resource(culled_cmd_bufs).buffer, rp.batch.cmd_buf, 0, { 0, rp.batch.cmd_buf->size });
//         r->sbuf->copy(rg->get_resource(culled_id_bufs).buffer, &ZERO, 0, 4);
//         q->wait_sync(r->sbuf->flush(), PipelineStage::COMPUTE_BIT);
//
//         cmd->bind_pipeline(cull_pipeline.get());
//         cmd->bind_resource(0, r->get_perframe().constants);
//         cmd->bind_resource(1, rp.batch.ids_buf);
//         cmd->bind_resource(2, rg->get_resource(culled_id_bufs).buffer);
//         cmd->bind_resource(3, rg->get_resource(culled_cmd_bufs).buffer, { batches[pfi].cmd_start, ~0ull });
//         cmd->bind_resource(4, r->make_texture(TextureDescriptor{ rg->get_resource(hiz).image->default_view, ImageLayout::GENERAL }));
//         cmd->dispatch((rp.batch.ids_count + 31) / 32, 1, 1);
//     }
//     std::vector<gfx::Renderer::IndirectBatch> batches;
//     RenderGraph::ResourceView fwd_id_bufs;
//     RenderGraph::ResourceView fwd_cmd_bufs;
//     RenderGraph::ResourceView culled_id_bufs;
//     RenderGraph::ResourceView culled_cmd_bufs;
//     RenderGraph::ResourceView hiz;
//     const gfx::Renderer::RenderPass* fwd{};
//     Handle<Pipeline> cull_pipeline;
// };
} // namespace culling
//
// namespace fwdp
//{
// class LightCulling : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         RenderGraph::ResourceView zbufs;
//         uint32_t num_tiles;
//         uint32_t lights_per_tile;
//         uint32_t tile_pixels;
//     };
//     LightCulling(RenderGraph* g, const CreateInfo& info) : Pass("fwdp::LightCulling", RenderOrder::DEFAULT_UNLIT)
//     {
//         num_tiles = info.num_tiles;
//         lights_per_tile = info.lights_per_tile;
//         tile_pixels = info.tile_pixels;
//         auto& r = get_renderer();
//         zbufs = info.zbufs;
//         const auto light_list_size = info.num_tiles * info.lights_per_tile * sizeof(uint32_t) + 128;
//         const auto light_grid_size = info.num_tiles * 2 * sizeof(uint32_t);
//         culled_light_list_bufs =
//             g->make_resource(BufferDescriptor{ "fwdp light list", light_list_size, BufferUsage::STORAGE_BIT }, r->frame_count);
//         culled_light_grid_bufs =
//             g->make_resource(BufferDescriptor{ "fwdp light grid", light_grid_size, BufferUsage::STORAGE_BIT }, r->frame_count);
//         light_culling_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
//             .shaders = { get_engine().renderer->make_shader("forwardp/cull_lights.comp.glsl") },
//             .layout = r->bindless_pplayout,
//         });
//     }
//     void setup() override
//     {
//         access(culled_light_list_bufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
//         access(culled_light_grid_bufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
//         access(zbufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         cmd->bind_pipeline(light_culling_pipeline.get());
//         cmd->bind_resource(0, r->get_perframe().constants);
//         cmd->bind_resource(1, rg->get_resource(culled_light_grid_bufs).buffer);
//         cmd->bind_resource(2, rg->get_resource(culled_light_list_bufs).buffer);
//         cmd->bind_resource(3, r->make_texture(TextureDescriptor{ rg->get_resource(zbufs).image->default_view,
//                                                                  ImageLayout::GENERAL, true }));
//
//         const uint32_t zero = 0u;
//         r->sbuf->copy(rg->get_resource(culled_light_list_bufs).buffer, &zero, 0ull, 4);
//         q->wait_sync(r->sbuf->flush(), PipelineStage::COMPUTE_BIT);
//
//         const auto* w = get_engine().window;
//         auto dx = (uint32_t)w->width;
//         auto dy = (uint32_t)w->height;
//         dx = (dx + tile_pixels - 1) / tile_pixels;
//         dy = (dy + tile_pixels - 1) / tile_pixels;
//         cmd->dispatch(dx, dy, 1);
//     }
//     uint32_t num_tiles;
//     uint32_t lights_per_tile;
//     uint32_t tile_pixels;
//     RenderGraph::ResourceView zbufs;
//     RenderGraph::ResourceView culled_light_list_bufs;
//     RenderGraph::ResourceView culled_light_grid_bufs;
//     Handle<Pipeline> light_culling_pipeline;
// };
//
// } // namespace fwdp
//
// class DefaultUnlit : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         const culling::ZPrepass* pzprepass;
//         const fwdp::LightCulling* plightculling;
//         RenderGraph::ResourceView cbufs;
//         RenderGraph::ResourceView zbufs;
//     };
//     DefaultUnlit(RenderGraph* g, const CreateInfo& info) : Pass("DefaultUnlit", RenderOrder::DEFAULT_UNLIT)
//     {
//         auto& r = get_renderer();
//         culled_id_bufs = info.pzprepass->culled_id_bufs;
//         culled_cmd_bufs = info.pzprepass->culled_cmd_bufs;
//         culled_light_list_bufs = info.plightculling->culled_light_list_bufs;
//         culled_light_grid_bufs = info.plightculling->culled_light_grid_bufs;
//         zbufs = info.zbufs;
//         cbufs = info.cbufs;
//     }
//     void setup() override
//     {
//         access(culled_id_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
//         access(culled_cmd_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
//         access(culled_light_grid_bufs, PipelineStage::FRAGMENT, PipelineAccess::SHADER_READ_BIT);
//         access(culled_light_list_bufs, PipelineStage::FRAGMENT, PipelineAccess::SHADER_READ_BIT);
//         access(zbufs, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_READ_BIT, ImageLayout::ATTACHMENT);
//         access(cbufs, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, ImageLayout::ATTACHMENT, true);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         r->render(RenderPassType::FORWARD, q, cmd);
//     }
//     RenderGraph::ResourceView culled_id_bufs;
//     RenderGraph::ResourceView culled_cmd_bufs;
//     RenderGraph::ResourceView culled_light_list_bufs;
//     RenderGraph::ResourceView culled_light_grid_bufs;
//     RenderGraph::ResourceView zbufs;
//     RenderGraph::ResourceView cbufs;
// };
//
// class DebugGeom : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         RenderGraph::ResourceView cbufs;
//         RenderGraph::ResourceView zbufs;
//     };
//     DebugGeom(RenderGraph* g, const CreateInfo& info) : Pass("DebugGeom", RenderOrder::DEFAULT_UNLIT)
//     {
//         auto& r = get_renderer();
//         zbufs = info.zbufs;
//         cbufs = info.cbufs;
//         debug_pipeline = r->make_pipeline(PipelineCreateInfo{
//             .shaders = { r->make_shader("debug/geom.vert.glsl"), r->make_shader("debug/geom.frag.glsl") },
//             .layout = r->bindless_pplayout,
//             .depth_test = false,
//             .depth_write = false,
//             .topology = Topology::LINE_LIST,
//             .polygon_mode = PolygonMode::FILL,
//             .culling = CullFace::NONE,
//             .line_width = 1.0f,
//         });
//     }
//     void setup() override
//     {
//         auto& r = get_renderer();
//         // access(zbufs, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_READ_BIT, ImageLayout::ATTACHMENT);
//         access(cbufs, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, ImageLayout::ATTACHMENT, true);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
//         const auto& cbuf = rg->get_resource(cbufs.get()).image.get();
//         VkViewport vkview{ 0.0f, 0.0f, (float)cbuf.width, (float)cbuf.height, 0.0f, 1.0f };
//         VkRect2D vksciss{ {}, { (uint32_t)cbuf.width, (uint32_t)cbuf.height } };
//
//         const VkRenderingAttachmentInfo vkcols[] = {
//             Vks(VkRenderingAttachmentInfo{ .imageView = cbuf.default_view->md.vk->view,
//                                            .imageLayout = to_vk(ImageLayout::ATTACHMENT),
//                                            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
//                                            .storeOp = VK_ATTACHMENT_STORE_OP_STORE }),
//         };
//         const auto vkreninfo = Vks(VkRenderingInfo{
//             .renderArea = vksciss,
//             .layerCount = 1,
//             .colorAttachmentCount = std::size(vkcols),
//             .pColorAttachments = vkcols,
//         });
//         cmd->set_scissors(&vksciss, 1);
//         cmd->set_viewports(&vkview, 1);
//         cmd->bind_pipeline(debug_pipeline.get());
//         cmd->bind_resource(0, r->get_perframe().constants);
//         cmd->begin_rendering(vkreninfo);
//         r->debug_bufs.render(cmd, nullptr);
//         cmd->end_rendering();
//     }
//     RenderGraph::ResourceView zbufs;
//     RenderGraph::ResourceView cbufs;
//     Handle<Pipeline> debug_pipeline;
// };
//
// class ImGui : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         RenderGraph::ResourceView cbufs;
//     };
//     ImGui(RenderGraph* g, const CreateInfo& info) : Pass("ImGui", RenderOrder::DEFAULT_UNLIT) { cbufs = info.cbufs; }
//     void setup() override
//     {
//         access(cbufs, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, ImageLayout::ATTACHMENT);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         auto& r = get_renderer();
//         r->imgui_renderer->update(cmd, rg->get_resource(cbufs).image->default_view);
//     }
//     RenderGraph::ResourceView cbufs;
// };
//
// class PresentCopy : public RenderGraph::Pass
//{
//   public:
//     struct CreateInfo
//     {
//         RenderGraph::ResourceView cbufs;
//         RenderGraph::ResourceView swapcbufs;
//         const gfx::Swapchain* swapchain;
//     };
//     PresentCopy(RenderGraph* g, const CreateInfo& info) : Pass("PresentCopy", RenderOrder::DEFAULT_UNLIT)
//     {
//         cbufs = info.cbufs;
//         swapcbufs = info.swapcbufs;
//         swapchain = info.swapchain;
//     }
//     void setup() override
//     {
//         access(cbufs, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT, ImageLayout::TRANSFER_SRC);
//         swapcbufs.set(swapchain->current_index);
//         access(swapcbufs, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::TRANSFER_DST);
//     }
//     void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
//     {
//         cmd->copy(swapchain->get_image().get(), rg->get_resource(cbufs).image.get());
//     }
//     RenderGraph::ResourceView cbufs;
//     RenderGraph::ResourceView swapcbufs;
//     const gfx::Swapchain* swapchain;
// };

} // namespace pass

} // namespace gfx

} // namespace eng