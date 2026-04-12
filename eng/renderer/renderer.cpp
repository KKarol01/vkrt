#include <meshoptimizer/src/meshoptimizer.h>
#include <ranges>
#include "renderer.hpp"
#include <eng/renderer/staging_buffer.hpp>
#include <eng/engine.hpp>
#include <eng/camera.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/renderer/vulkan/to_vk.hpp>
#include <eng/common/to_string.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/ecs/components.hpp>
#include <eng/assets/asset_manager.hpp>
#include <eng/renderer/passes/passes.hpp>
#include <eng/scene.hpp>
#include <eng/renderer/passes/renderpass.hpp>
#include <eng/math/align.hpp>
#include <assets/shaders/common.hlsli>

namespace eng
{

namespace gfx
{

ImageBlockData get_block_data(ImageFormat format)
{
    switch(format)
    {
    case ImageFormat::R8G8B8A8_UNORM:
    case ImageFormat::R8G8B8A8_SRGB:
    {
        return { 4, { 1, 1, 1 } };
    }
    case ImageFormat::R16FG16FB16FA16F:
    {
        return { 8, { 1, 1, 1 } };
    }
    default:
    {
        ENG_ASSERT(false && "Bad format.");
        return {};
    }
    }
}

void Renderer::init(IRendererBackend* backend)
{
    this->backend = backend;
    backend->init();

    gq = backend->get_queue(QueueType::GRAPHICS);
    staging = new StagingBuffer{};
    staging->init(gq);
    rgraph = new RGRenderGraph{};
    rgraph->init(this);

    init_bufs();
    init_perframes();
    init_pipelines();
    init_helper_geom();

    imgui_renderer = new ImGuiRenderer{};
    imgui_renderer->init();

    settings.new_render_resolution = { get_engine().window->width, get_engine().window->height };
    settings.present_resolution = { get_engine().window->width, get_engine().window->height };
    settings.regenerate_swapchain = true;

    get_engine().window->add_on_resize([this](float x, float y) {
        if(settings.present_resolution.x != x || settings.present_resolution.y != y)
        {
            settings.present_resolution = { x, y };
            settings.regenerate_swapchain = true;
        }
        return true;
    });

    get_engine().ecs->register_callbacks<ecs::Mesh>([this](ecs::EntityId e) {
        auto& ecs = *get_engine().ecs;
        auto& mesh = ecs.get<ecs::Mesh>(e);
        if(mesh.gpu_resource == ~0u)
        {
            mesh.gpu_resource = *gpu_resource_allocator.allocate();
            new_transforms.push_back(e);
        }
        for(const auto& rm : mesh.render_meshes)
        {
            const auto& mat = rm->material.get();
            const auto& mp = mat.mesh_pass.get();

            for(auto i = 0u; i < (uint32_t)RenderPassType::LAST_ENUM; ++i)
            {
                if(mp.effects[i]) { mesh_render_data[i].add_mesh(mesh.gpu_resource, rm); }
            }
        }
    });

    new_shaders_listener = get_engine().assets->make_listener();
    get_engine().assets->listen_for_path("/assets/shaders", new_shaders_listener);
    // get_engine().assets->listen_for_path("/eng/renderer/shaders", new_shaders_listener);

    for(auto i = 0u; i < (uint32_t)RenderPassType::LAST_ENUM; ++i)
    {
        MeshRenderData rp{};
        rp.type = (RenderPassType)i;
        mesh_render_data[i] = rp;
    }
}

void Renderer::init_helper_geom()
{
    // std::vector<Vertex> vertices;
    // std::vector<uint32_t> indices;
    // const auto gen_uv_sphere = [&vertices, &indices] {
    //     const auto segs = 16;
    //     const auto rings = 16;
    //     vertices.clear();
    //     indices.clear();
    //     vertices.reserve(segs * rings);
    //     indices.reserve((rings - 1) * (segs - 1) * 6);
    //     for(auto y = 0u; y < rings; ++y)
    //     {
    //         const auto v = (float)y / (float)(rings - 1);
    //         const auto theta = v * glm::pi<float>();
    //         const auto st = std::sinf(theta);
    //         const auto ct = std::cosf(theta);
    //         for(auto x = 0u; x < segs; ++x)
    //         {
    //             const auto u = (float)x / (float)(segs - 1);
    //             const auto phi = u * 2.0f * glm::pi<float>();
    //             const auto sp = std::sinf(phi);
    //             const auto cp = std::cosf(phi);
    //             vertices.push_back(Vertex{ .position = { st * cp, ct, st * sp }, .uv = { u, v } });
    //         }
    //     }
    //     for(auto y = 0u; y < rings - 1; ++y)
    //     {
    //         for(auto x = 0u; x < segs - 1; ++x)
    //         {
    //             const auto idx = y * segs + x;
    //             indices.push_back(idx);
    //             indices.push_back(idx + 1);
    //             indices.push_back(idx + segs);
    //             indices.push_back(idx + segs);
    //             indices.push_back(idx + 1);
    //             indices.push_back(idx + segs + 1);
    //         }
    //     }
    // };

    // gen_uv_sphere();
    // ENG_ASSERT(vertices.size() <= ~uint16_t{});
    // helpergeom.uvsphere = make_geometry(GeometryDescriptor{ .vertices = vertices, .indices = indices });
    // helpergeom.ppskybox = get_engine().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_engine().renderer->make_shader("common/skybox.vert.glsl"),
    //                  get_engine().renderer->make_shader("common/skybox.frag.glsl") },
    //     .layout = common_playout,
    //     .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
    //     .depth_test = true,
    //     .depth_write = true,
    //     .depth_compare = DepthCompare::GREATER,
    //     .culling = CullFace::BACK,
    // });
}

void Renderer::init_pipelines()
{
    auto linear_sampler = make_sampler(Sampler::init(ImageFilter::LINEAR, ImageAddressing::REPEAT));
    auto nearest_sampler = make_sampler(Sampler::init(ImageFilter::NEAREST, ImageAddressing::REPEAT));
    auto hiz_sampler = make_sampler(Sampler::init(ImageFilter::LINEAR, ImageFilter::LINEAR, ImageAddressing::CLAMP_EDGE,
                                                  ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE,
                                                  SamplerMipmapMode::NEAREST, 0.0f, 1000.0f, 0.0f, SamplerReductionMode::MIN));
    Handle<Sampler> imsamplers[3]{};
    imsamplers[ENG_SAMPLER_LINEAR] = linear_sampler;
    imsamplers[ENG_SAMPLER_NEAREST] = nearest_sampler;
    imsamplers[ENG_SAMPLER_HIZ] = hiz_sampler;

    if(backend->caps.supports_bindless)
    {
        const auto common_dlayout = make_layout(DescriptorLayout{
            .layout = {
                { DescriptorType::STORAGE_BUFFER, ENG_BINDLESS_STORAGE_BUFFER_BINDING, 1024, ShaderStage::ALL },
                { DescriptorType::STORAGE_IMAGE, ENG_BINDLESS_STORAGE_IMAGE_BINDING, 1024, ShaderStage::ALL },
                { DescriptorType::SAMPLED_IMAGE, ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 1024, ShaderStage::ALL },
                { DescriptorType::SEPARATE_SAMPLER, ENG_BINDLESS_SAMPLER_BINDING, std::size(imsamplers), ShaderStage::ALL, imsamplers },
                { DescriptorType::ACCELERATION_STRUCTURE, ENG_BINDLESS_ACCELERATION_STRUCT_BINDING, 8, ShaderStage::ALL },
            },
			.md = {},
			});
        settings.common_layout = make_layout(PipelineLayout{
            .layout = { common_dlayout },
            .push_range = { ShaderStage::ALL, PushRange::MAX_PUSH_BYTES },
            .md = {},
        });
        descriptor_allocator = new DescriptorSetAllocatorBindlessVk{ settings.common_layout.get() };
    }
    else
    {
        ENG_ERROR("Nonbindless path not supported.");
        return;
    }

    // hiz_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_engine().renderer->make_shader("culling/hiz.comp.glsl") }, .layout = common_playout });
    // cull_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_engine().renderer->make_shader("culling/culling.comp.glsl") },
    //     .layout = common_playout,
    // });
    // cullzout_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_engine().renderer->make_shader("common/zoutput.vert.glsl"),
    //                  get_engine().renderer->make_shader("common/zoutput.frag.glsl") },
    //     .layout = common_playout,
    //     .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
    //     .depth_test = true,
    //     .depth_write = true,
    //     .depth_compare = DepthCompare::GREATER,
    //     .culling = CullFace::BACK,
    // });
    // fwdp_cull_lights_pipeline = get_engine().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_engine().renderer->make_shader("forwardp/cull_lights.comp.glsl") },
    //     .layout = common_playout,
    // });

    {
        settings.default_z_prepass_pipeline =
            make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/default_z_prepass/z_prepass.vs.hlsl",
                                                     "/assets/shaders/default_z_prepass/z_prepass.ps.hlsl" })
                              .init_image_attachments(PipelineCreateInfo::AttachmentState{
                                  .count = 0, .color_formats = {}, .blend_states = {}, .depth_format = settings.depth_format })
                              .init_depth_test(true, true, settings.rw_depth_compare)
                              .init_topology(Topology::TRIANGLE_LIST, PolygonMode::FILL, CullFace::BACK));

        settings.default_forward_pipeline =
            make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/default_unlit/default_unlit.vs.hlsl",
                                                     "/assets/shaders/default_unlit/default_unlit.ps.hlsl" })
                              .init_image_attachments(PipelineCreateInfo::AttachmentState{
                                  .count = 1,
                                  .color_formats = { settings.color_format },
                                  .blend_states = { PipelineCreateInfo::BlendState{ .enable = true,
                                                                                    .src_color_factor = BlendFactor::SRC_ALPHA,
                                                                                    .dst_color_factor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                                                                                    .color_op = BlendOp::ADD,
                                                                                    .src_alpha_factor = BlendFactor::ONE,
                                                                                    .dst_alpha_factor = BlendFactor::ZERO,
                                                                                    .alpha_op = BlendOp::ADD } },
                                  .depth_format = settings.depth_format })
                              .init_depth_test(true, false, settings.read_depth_compare)
                              .init_topology(Topology::TRIANGLE_LIST, PolygonMode::FILL, CullFace::BACK));
        settings.apply_ao_pipeline = make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/ssao/apply.cs.hlsl" }));
    }

    {
        const auto zprepass_effect = make_shader_effect(ShaderEffect{ .pipeline = settings.default_z_prepass_pipeline });
        const auto unlit_effect = make_shader_effect(ShaderEffect{ .pipeline = settings.default_forward_pipeline });
        MeshPass::Effects effects{};
        effects[(int)RenderPassType::Z_PREPASS] = zprepass_effect;
        effects[(int)RenderPassType::OPAQUE] = unlit_effect;

        settings.default_meshpass = make_mesh_pass(MeshPass::init("mesh_pass_default_unlit", effects));
        settings.default_material =
            materials.insert(Material::init("material_default_opaque", settings.default_meshpass)).handle;

        settings.default_base_color =
            make_image("ENG_default_base_color",
                       Image::init(2, 2, ImageFormat::R8G8B8A8_UNORM, ImageUsage::SAMPLED_BIT, ImageLayout::READ_ONLY));
        uint8_t data[] = {
            255, 0, 255, 255, // Pixel 1: Magenta
            0,   0, 0,   255, // Pixel 2: Black
            0,   0, 0,   255, // Pixel 3: Black
            255, 0, 255, 255  // Pixel 4: Magenta
        };
        staging->copy(settings.default_base_color.get(), data, 0, 0);
    }

    {
        passes.reconstruct_normals = std::make_shared<pass::NormalFromDepth>();
        passes.ao[(int)AOMode::SSAO] = std::make_shared<pass::SSAO>();
        passes.ao[(int)AOMode::GTAO] = std::make_shared<pass::GTAO>();
        passes.ao[(int)AOMode::RTAO] = std::make_shared<pass::RTAO>();
        passes.mesh_passes[(int)RenderPassType::Z_PREPASS] = std::make_shared<pass::ZPrepass>(RenderPassType::Z_PREPASS);
        passes.mesh_passes[(int)RenderPassType::OPAQUE] = std::make_shared<pass::MeshPass>(RenderPassType::OPAQUE);
    }
}

void Renderer::init_perframes()
{
    frame_datas.resize(frame_delay);
    current_data = &frame_datas[0];
    for(auto i = 0u; i < frame_delay; ++i)
    {
        frame_datas[i].cmdpool = gq->make_command_pool();
        frame_datas[i].acq_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("acquire semaphore {}", i) });
        frame_datas[i].ren_fen = make_sync({ SyncType::FENCE, 1, ENG_FMT("rendering fence {}", i) });
        frame_datas[i].swp_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 1, ENG_FMT("swap semaphore {}", i) });
        frame_datas[i].timestamp_pool = backend->make_query_pool({ QueryType::TIMESTAMP, 1024 });
    }
}

void Renderer::init_bufs()
{
    bufs.positions = make_buffer("vertex positions", Buffer::init(1024, BufferUsage::STORAGE_BIT | BufferUsage::AS_BUILD_INPUT));
    bufs.attributes = make_buffer("vertex attributes", Buffer::init(1024, BufferUsage::STORAGE_BIT));
    bufs.indices = make_buffer("vertex indices", Buffer::init(1024, BufferUsage::STORAGE_BIT | BufferUsage::INDEX_BIT |
                                                                        BufferUsage::AS_BUILD_INPUT));
    bufs.bspheres = make_buffer("bounding spheres", Buffer::init(1024, BufferUsage::STORAGE_BIT));
    bufs.materials = make_buffer("materials", Buffer::init(1024, BufferUsage::STORAGE_BIT));
    for(uint32_t i = 0; i < 2; ++i)
    {
        bufs.transforms[i] = make_buffer(ENG_FMT("trs {}", i), Buffer::init(1024, BufferUsage::STORAGE_BIT));
        bufs.lights[i] = make_buffer(ENG_FMT("lights {}", i), Buffer::init(1024, BufferUsage::STORAGE_BIT));
    }
    {
        const auto* w = get_engine().window;
        const auto num_tiles_x = (uint32_t)std::ceilf(w->width / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles_y = (uint32_t)std::ceilf(w->height / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles = num_tiles_x * num_tiles_y;
        bufs.fwdp_num_tiles = num_tiles;
    }
}

// void Renderer::init_rgraph_passes()
//{
// ENG_ASSERT(rgraph_passes.empty());
// rgraph_passes.push_back(new pass::SSTriangle{});

// for(auto& pass : rgraph_passes)
//{
//     pass->init();
// }

// rgraphpasses = new RenderGraphPasses{};
// std::vector<Handle<Image>> zbufs(frame_count);
// std::vector<Handle<Image>> cbufs(frame_count);
// std::vector<Handle<Image>> swapcbufs(frame_count);
// for(auto i = 0u; i < frame_count; ++i)
//{
//     zbufs[i] = perframe[i].gbuffer.depth;
//     cbufs[i] = perframe[i].gbuffer.color;
//     swapcbufs[i] = swapchain->images[i];
// }
// rgraphpasses->zbufsview = rgraph->import_resource(std::span{ zbufs });
// rgraphpasses->cbufsview = rgraph->import_resource(std::span{ cbufs });
// rgraphpasses->swapcbufsview = rgraph->import_resource(std::span{ swapcbufs });

// rgraphpasses->cull_zprepass =
//     std::make_unique<pass::culling::ZPrepass>(rgraph, pass::culling::ZPrepass::CreateInfo{ rgraphpasses->zbufsview });
// rgraphpasses->cull_hiz =
//     std::make_unique<pass::culling::Hiz>(rgraph, pass::culling::Hiz::CreateInfo{ rgraphpasses->zbufsview });
// rgraphpasses->cull_main =
//     std::make_unique<pass::culling::MainPass>(rgraph, pass::culling::MainPass::CreateInfo{
//                                                           &render_passes.at(RenderPassType::FORWARD),
//                                                           &*rgraphpasses->cull_zprepass, &*rgraphpasses->cull_hiz });

// rgraphpasses->cull_zprepass->ibatches = &rgraphpasses->cull_main->batches; // todo: don't like this

// rgraphpasses->fwdp_lightcull =
//     std::make_unique<pass::fwdp::LightCulling>(rgraph, pass::fwdp::LightCulling::CreateInfo{
//                                                            rgraphpasses->zbufsview, bufs.fwdp_num_tiles,
//                                                            bufs.fwdp_lights_per_tile, bufs.fwdp_tile_pixels });
// rgraphpasses->default_unlit =
//     std::make_unique<pass::DefaultUnlit>(rgraph, pass::DefaultUnlit::CreateInfo{
//                                                      &*rgraphpasses->cull_zprepass, &*rgraphpasses->fwdp_lightcull,
//                                                      rgraphpasses->cbufsview, rgraphpasses->zbufsview });
// rgraphpasses->debug_geom =
//     std::make_unique<pass::DebugGeom>(rgraph, pass::DebugGeom::CreateInfo{ rgraphpasses->cbufsview, rgraphpasses->zbufsview });
// rgraphpasses->imgui = std::make_unique<pass::ImGui>(rgraph, pass::ImGui::CreateInfo{ rgraphpasses->cbufsview });
// rgraphpasses->present_copy =
//     std::make_unique<pass::PresentCopy>(rgraph, pass::PresentCopy::CreateInfo{ rgraphpasses->cbufsview,
//                                                                                rgraphpasses->swapcbufsview, swapchain });
//}

// void Renderer::instance_entity(ecs::EntityId e)
//{
// ENG_ASSERT(false);
//  if(!get_engine().ecs->has<ecs::Transform, ecs::Mesh>(e))
//{
//      ENG_WARN("Entity {} does not have the required components (Transform, Mesh).", *e);
//      return;
//  }
//  auto& mesh = get_engine().ecs->get<ecs::Mesh>(e);
//  if(mesh.gpu_resource != ~0u)
//{
//      ENG_WARN("Entity {} with mesh {} is already instanced {}", *e, mesh.asset->name, mesh.gpu_resource);
//      return;
//  }
//++bufs.transform_count;
//  mesh.gpu_resource = gpu_resource_allocator.allocate();
//  for(const auto& rmesh : mesh.asset->render_meshes)
//{
//      const auto& mpeffect = rmesh->material->mesh_pass->effects;
//      for(auto i = 0u; i < mpeffect.size(); ++i)
//      {
//          if(mpeffect.at(i)) { render_passes.get((RenderPassType)i).entities.push_back(e); }
//      }
//  }
//}

void Renderer::update()
{
    settings.render_resolution = settings.new_render_resolution;

    if(settings.regenerate_swapchain)
    {
        if(swapchain)
        {
            vkDeviceWaitIdle(RendererBackendVk::get_dev());
            backend->destroy_swapchain(swapchain);
            swapchain = nullptr;
        }
        if(settings.present_resolution.x > 0.0f && settings.present_resolution.y > 0.0f)
        {
            settings.regenerate_swapchain = false;
            swapchain = backend->make_swapchain();
        }
        else { return; }
    }

    current_data->ren_fen->wait_cpu(~0ull);
    current_data->ren_fen->reset();
    current_data->cmdpool->reset();
    current_data->available_queries = std::move(current_data->timestamp_queries);
    // Stupid swapchain -- cannot reset those (binary sems that need to be destroyed to reset)
    // because waiting on a ren_fen doesn't guarantee that sems are no longer in use, and which
    // get reset only when waited on in the queue submission, so acq_sem gets waited on (reset)
    // during (but before) ren_fen signal operation, and swp_sem during present.
    // current_data->acq_sem->reset();
    // current_data->swp_sem->reset();

    if(current_data->retired_resources.size() > 0)
    {
        // ENG_LOG("Removing {} retired resources", current_data->retired_resources.size());
        auto remove_until = current_data->retired_resources.begin();
        for(auto& rs : current_data->retired_resources)
        {
            if(current_frame - rs.deleted_at_frame < frame_delay) { break; }
            ++remove_until;
            if(auto* buf = std::get_if<Handle<Buffer>>(&rs.resource))
            {
                // ENG_LOG("Removing retired buffer {} ({})", buffer_names[*(*buf)], *(*buf));
                backend->destroy_buffer(buf->get());
                buffers.erase(Slotmap<Buffer, 1024>::SlotId{ buf->handle });
            }
            else if(auto* img = std::get_if<Handle<Image>>(&rs.resource))
            {
                // ENG_LOG("Removing retired image {} ({})", image_names[*(*img)], *(*img));
                backend->destroy_image(img->get());
                images.erase(Slotmap<Image, 1024>::SlotId{ img->handle });
            }
        }
        current_data->retired_resources.erase(current_data->retired_resources.begin(), remove_until);
    }

    new_shaders_listener->consume_paths([this](std::vector<fs::Path> paths) {
        if(paths.empty()) { return; }
        vkDeviceWaitIdle(RendererBackendVk::get_dev());
        const auto to_recompile = [this, &paths] {
            std::vector<Handle<Shader>> ret;
            ret.reserve(paths.size());
            for(const auto& p : paths)
            {
                auto it = std::ranges::find_if(shaders, [&p](const auto& sh) { return sh.path == p; });
                if(it != shaders.end()) { ret.emplace_back((uint32_t)std::distance(shaders.begin(), it)); }
            }
            return ret;
        }();
        for(auto shaderh : to_recompile)
        {
            auto new_shader = Shader::init(shaderh->path);
            backend->make_shader(new_shader);
            const auto compilation_res = backend->compile_shader(new_shader);
            if(!compilation_res)
            {
                backend->destroy_shader(new_shader);
                continue;
            }
            backend->destroy_shader(shaderh.get());
            ENG_ASSERT(!shaderh->md.ptr && new_shader.md.ptr);
            shaderh->md = new_shader.md;
            for(auto pipelineh : shader_usage_pipeline_map[shaderh])
            {
                backend->destroy_pipeline(pipelineh.get());
                backend->make_pipeline(pipelineh.get());
                new_pipelines.push_back(pipelineh);
            }
            shader_usage_pipeline_map[shaderh].clear();
        }
    });

    if(new_shaders.size())
    {
        for(auto& e : new_shaders)
        {
            backend->compile_shader(e.get());
        }
        new_shaders.clear();
    }
    if(new_pipelines.size())
    {
        for(auto pipelineh : new_pipelines)
        {
            for(auto shaderh : pipelineh->info.shaders)
            {
                shader_usage_pipeline_map[shaderh].push_back(pipelineh);
            }
            backend->compile_pipeline(pipelineh.get());
        }
        new_pipelines.clear();
    }
    if(new_materials.size())
    {
        for(const auto& e : new_materials)
        {
            // use stable handle index inside the storage to index it in the gpu
            if(backend->caps.supports_bindless)
            {
                GPUMaterial gpumat{ .base_color_idx =
                                        descriptor_allocator->get_bindless(DescriptorResource::sampled_image(e->base_color_texture)) };
                staging->copy(bufs.materials.get(), &gpumat, *e * sizeof(gpumat), sizeof(gpumat));
            }
            else { ENG_ASSERT(false); }
        }
        new_materials.clear();
    }
    if(new_transforms.size())
    {
        std::swap(bufs.transforms[0], bufs.transforms[1]);
        const auto req_size = gpu_resource_allocator.size() * sizeof(glm::mat4);
        resize_buffer(bufs.transforms[0], req_size, false);
        staging->copy(bufs.transforms[0].get(), bufs.transforms[1].get(), 0, { 0, bufs.transforms[1]->size }, true);
        for(auto i = 0u; i < new_transforms.size(); ++i)
        {
            const auto entity = new_transforms[i];
            const auto [transform, mesh] = get_engine().ecs->get<ecs::Transform, ecs::Mesh>(entity);
            const auto trsmat4x4 = transform.to_mat4();
            staging->copy(bufs.transforms[0].get(), &trsmat4x4[0][0], mesh.gpu_resource * sizeof(glm::mat4), sizeof(glm::mat4));
        }
        new_transforms.clear();
    }
    if(new_lights.size())
    {
        // ENG_ASSERT(false);
        //  std::swap(bufs.lights[0], bufs.lights[1]);
        //  const auto req_size = bufs.light_count * sizeof(GPULight) + 4;
        //  resize_buffer(bufs.lights[0], req_size, false);
        //  staging->copy(bufs.lights[0], bufs.lights[1], 0, { 0, bufs.lights[1]->size }, true);
        //  for(auto i = 0u; i < new_lights.size(); ++i)
        //{
        //      auto& l = get_engine().ecs->get<ecs::Light>(new_lights[i]);
        //      const auto& t = get_engine().ecs->get<ecs::Transform>(new_lights[i]);
        //      if(l.gpu_index == ~0u) { l.gpu_index = gpu_light_allocator.allocate(); }
        //      GPULight gpul{ t.pos(), l.range, l.color, l.intensity, (uint32_t)l.type };
        //      staging->copy(bufs.lights[0], &gpul, offsetof(GPULightsBuffer, lights_us) + l.gpu_index * sizeof(GPULight),
        //                    sizeof(GPULight));
        //  }
        //  staging->copy(bufs.lights[0], &bufs.light_count, offsetof(GPULightsBuffer, count), 4);
        //  new_lights.clear();
    }
    if(new_blases.size())
    {
        staging->get_wait_sem()->wait_cpu(~0ull);
        std::vector<ASRequirements> reqs_vec;
        ASRequirements total_reqs{};
        for(auto gh : new_blases)
        {
            auto& reqs = reqs_vec.emplace_back();
            backend->make_blas(gh.get(), reqs, nullptr, nullptr, 0, nullptr, 0);
            reqs.acceleration_structure_size = align_up2(reqs.acceleration_structure_size, 256);
            ENG_ASSERT(reqs.build_scratch_size % backend->props.min_acceleration_structure_scratch_offset_alignment == 0);
            total_reqs.acceleration_structure_size += reqs.acceleration_structure_size;
            total_reqs.build_scratch_size += reqs.build_scratch_size;
        }

        if(!bufs.geom_blas)
        {
            bufs.geom_blas =
                make_buffer("blas buffer", Buffer::init(total_reqs.acceleration_structure_size, BufferUsage::AS_STORAGE));
        }
        if(bufs.geom_blas->capacity < total_reqs.acceleration_structure_size)
        {
            backend->destroy_buffer(bufs.geom_blas.get());
            bufs.geom_blas->capacity = total_reqs.acceleration_structure_size;
            backend->allocate_buffer(bufs.geom_blas.get());
        }
        auto blas_scratch = Buffer::init(total_reqs.build_scratch_size, BufferUsage::AS_SCRATCH);
        backend->allocate_buffer(blas_scratch);

        auto* cmd = current_data->cmdpool->begin();
        total_reqs = {};
        for(auto i = 0u; i < reqs_vec.size(); ++i)
        {
            ASRequirements reqs = reqs_vec[i];
            ASRequirements _reqs;
            backend->make_blas(new_blases[i].get(), _reqs, cmd, &bufs.geom_blas.get(),
                               total_reqs.acceleration_structure_size, &blas_scratch, total_reqs.build_scratch_size);
            total_reqs.acceleration_structure_size += reqs.acceleration_structure_size;
            total_reqs.build_scratch_size += reqs.build_scratch_size;
        }
        current_data->cmdpool->end(cmd);
        gq->with_cmd_buf(cmd).submit_wait(~0ull);
        backend->destroy_buffer(blas_scratch);

        std::vector<uint32_t> instance_ids;
        std::vector<const Geometry*> tlas_geoms;
        std::vector<glm::mat3x4> trs;
        get_engine().ecs->iterate_components<ecs::Mesh, ecs::Transform>([&](ecs::EntityId e, const ecs::Mesh& m,
                                                                            const ecs::Transform& t) {
            for(auto rmh : m.render_meshes)
            {
                instance_ids.push_back(m.gpu_resource);
                tlas_geoms.push_back(&rmh->geometry.get());
                glm::mat4x3 mat = t.to_mat4();
                trs.push_back(glm::transpose(mat));
            }
        });

        ASRequirements tlas_reqs;
        backend->make_tlas(std::span{ tlas_geoms }, std::span{ std::as_const(trs) },
                           std::span{ std::as_const(instance_ids) }, tlas_reqs, nullptr, nullptr, 0, nullptr, 0, nullptr, 0);
        {
            auto scratch = Buffer::init(tlas_reqs.build_scratch_size, BufferUsage::AS_SCRATCH);
            bufs.geom_tlas_buffer =
                make_buffer("geom tlas", Buffer::init(tlas_reqs.acceleration_structure_size, BufferUsage::AS_STORAGE));
            auto instances = Buffer::init(tlas_reqs.instance_data_buffer_size, BufferUsage::AS_BUILD_INPUT);
            backend->allocate_buffer(scratch);
            backend->allocate_buffer(instances);
            auto* cmd = current_data->cmdpool->begin();
            bufs.geom_tlas = backend->make_tlas(std::span{ tlas_geoms }, std::span{ std::as_const(trs) },
                                                std::span{ std::as_const(instance_ids) }, tlas_reqs, cmd,
                                                &bufs.geom_tlas_buffer.get(), 0, &scratch, 0, &instances, 0);
            current_data->cmdpool->end(cmd);
            gq->with_cmd_buf(cmd).submit_wait(~0ull);
            backend->destroy_buffer(scratch);
            backend->destroy_buffer(instances);
        }

        new_blases.clear();
    }

    swapchain->acquire(~0ull, current_data->acq_sem);

    compile_rendergraph();

    Sync* rg_wait_syncs[]{ current_data->acq_sem, staging->get_wait_sem() };
    Sync* rgsync = rgraph->execute(&rg_wait_syncs[0], std::size(rg_wait_syncs));
    auto* cmd = current_data->cmdpool->begin();
    current_data->cmdpool->end(cmd);
    cmd->wait_sync(rgsync);
    cmd->signal_sync(current_data->swp_sem);
    cmd->signal_sync(current_data->ren_fen);
    gq->with_cmd_buf(cmd).submit();
    gq->wait_sync(current_data->swp_sem).present(swapchain);

    ++current_frame;
    current_data = &frame_datas[current_frame % frame_datas.size()];
}

void Renderer::compile_rendergraph()
{
    for(auto i = 0u; i < (uint32_t)RenderPassType::LAST_ENUM; ++i)
    {
        mesh_render_data[i].build();
    }

    current_data->render_resources = rgraph->add_graphics_pass<RenderResources>(
        "SETUP_TARGETS", RenderOrder::SETUP_TARGETS,
        [](RGBuilder& b, RenderResources& d) {
            auto constsacc = b.create_resource("constants", Buffer::init(sizeof(GPUEngConstants), BufferUsage::STORAGE_BIT));
            constsacc = b.write_buffer(constsacc);
            d.constants = b.as_res_id(constsacc);
        },
        [](RGBuilder& b, const RenderResources& d) {
            auto* c = get_engine().camera;
            GPUEngConstants constants_buffer{};
            constants_buffer.view = c->get_view();
            constants_buffer.proj = c->get_projection();
            constants_buffer.proj_view = c->get_projection() * c->get_view();
            constants_buffer.inv_view = glm::inverse(c->get_view());
            constants_buffer.inv_proj = glm::inverse(c->get_projection());
            constants_buffer.inv_proj_view = constants_buffer.inv_view * constants_buffer.inv_proj;
            constants_buffer.cam_pos = c->pos;

            auto* cmd = b.open_cmd_buf();
            get_renderer().staging->copy(get_renderer().rgraph->get_buf(b.as_acc_id(d.constants)).get(),
                                         &constants_buffer, 0ull, sizeof(constants_buffer), false);
            cmd->wait_sync(get_renderer().staging->get_wait_sem());
        });

    passes.mesh_passes[(int)RenderPassType::Z_PREPASS]->init(rgraph);
    passes.reconstruct_normals->init(rgraph);
    passes.ao[(int)settings.gfx_settings.ao_mode]->init(rgraph);
    passes.mesh_passes[(int)RenderPassType::OPAQUE]->init(rgraph);

    struct ApplyAOData
    {
        RGAccessId in_out_color;
        RGAccessId in_ao;
    };
    rgraph->add_graphics_pass<ApplyAOData>(
        "APPLY_AO", RenderOrder::MESH_RENDER,
        [this](RGBuilder& b, ApplyAOData& d) {
            d.in_out_color = b.read_write_image(b.as_acc_id(current_data->render_resources.color));
            d.in_ao = b.read_image(b.as_acc_id(current_data->render_resources.ao));
        },
        [this](RGBuilder& b, const ApplyAOData& d) {
            auto* cmd = b.open_cmd_buf();
            cmd->bind_pipeline(settings.apply_ao_pipeline.get());
            DescriptorResource resources[]{
                DescriptorResource::storage_image(b.graph->get_acc(d.in_out_color).image_view),
                DescriptorResource::storage_image(b.graph->get_acc(d.in_ao).image_view),
            };
            const auto& img = b.graph->get_img(d.in_ao).get();
            cmd->bind_resources(1, resources);
            cmd->dispatch((img.width + 7) / 8, (img.height + 7) / 8, 1);
        });

    const auto imdata = imgui_renderer->update(rgraph);

    struct CopySwapchainData
    {
        RGAccessId input;
        RGAccessId output;
    };
    const auto copyswapchaindata = rgraph->add_graphics_pass<CopySwapchainData>(
        "copy to swapchain", RenderOrder::PRESENT,
        [this, imdata](RGBuilder& pb, CopySwapchainData& data) {
            data.input = pb.access_resource(imdata.output, ImageLayout::TRANSFER_SRC, PipelineStage::TRANSFER_BIT,
                                            PipelineAccess::TRANSFER_READ_BIT);
            data.output = pb.import_resource(swapchain->get_image());
            data.output = pb.access_resource(data.output, ImageLayout::TRANSFER_DST, PipelineStage::TRANSFER_BIT,
                                             PipelineAccess::TRANSFER_WRITE_BIT);
        },
        [](RGBuilder& pb, const CopySwapchainData& data) {
            auto* cmd = pb.open_cmd_buf();
            cmd->copy(pb.graph->get_img(data.output).get(), pb.graph->get_img(data.input).get());
        });
    rgraph->add_graphics_pass<RGAccessId>(
        "present swapchain", RenderOrder::PRESENT,
        [copyswapchaindata](RGBuilder& pb, RGAccessId& output) {
            output = pb.access_resource(copyswapchaindata.output, ImageLayout::PRESENT, PipelineStage::ALL,
                                        PipelineAccess::TRANSFER_WRITE_BIT);
        },
        [](RGBuilder& pb, auto& data) {});

    rgraph->compile();
}

// void Renderer::render(RenderPassType pass, SubmitQueue* queue, CommandBufferVk* cmd)
//{
//  ENG_ASSERT(false);
//   auto* ew = get_engine().window;
//   auto& pf = get_perframe();
//   auto& rp = render_passes.at(pass);

// const VkRenderingAttachmentInfo vkcols[] = { Vks(VkRenderingAttachmentInfo{
//     .imageView = current_data->gbuffer.color->default_view->md.vk->view,
//     .imageLayout = to_vk(ImageLayout::ATTACHMENT),
//     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
//     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
//     .clearValue = { .color = { .uint32 = {} } } }) };
// const auto vkdep = Vks(VkRenderingAttachmentInfo{ .imageView = current_data->gbuffer.depth->default_view->md.vk->view,
//                                                   .imageLayout = to_vk(ImageLayout::ATTACHMENT),
//                                                   .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
//                                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE });
// VkViewport vkview{ 0.0f, 0.0f, get_engine().window->width, get_engine().window->height, 0.0f, 1.0f };
// VkRect2D vksciss{ {}, { (uint32_t)get_engine().window->width, (uint32_t)get_engine().window->height } };
// const auto vkreninfo = Vks(VkRenderingInfo{
//     .renderArea = vksciss, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = vkcols, .pDepthAttachment = &vkdep });

//// todo: mbatches might be empty. is this bad that i run this?
// cmd->bind_index(bufs.idx_buf.get(), 0, bufs.index_type);
// cmd->set_scissors(&vksciss, 1);
// cmd->set_viewports(&vkview, 1);
// cmd->begin_rendering(vkreninfo);
// const auto& maincullpass = rgraph->get_pass<pass::culling::MainPass>("culling::MainPass");
// const auto& ib = maincullpass.batches[get_perframe_index()];
// render_ibatch(cmd, ib, [this, &ib, &pf](CommandBuffer* cmd) {
//     const auto outputmode = (uint32_t)debug_output;
//     cmd->bind_resource(0, current_data->constants);
//     cmd->bind_resource(1, ib.ids_buf);
//     const auto& fwd = rgraph->get_pass<pass::fwdp::LightCulling>("fwdp::LightCulling");
//     cmd->bind_resource(2, rgraph->get_resource(fwd.culled_light_grid_bufs).buffer);
//     cmd->bind_resource(3, rgraph->get_resource(fwd.culled_light_list_bufs).buffer);
// });
// cmd->end_rendering();
//}

void Renderer::render_debug(const DebugGeometry& geom) { debug_bufs.add(geom); }

Handle<Buffer> Renderer::make_buffer(std::string_view name, Buffer&& buffer, AllocateMemory allocate)
{
    uint32_t order = 0;
    float size = (float)buffer.capacity;
    static constexpr const char* units[]{ "B", "KB", "MB", "GB" };
    for(; size >= 1024.0f && order < std::size(units); size /= 1024.0f, ++order) {}
    // ENG_LOG("Creating buffer {} [{:.2f} {}]", name, size, units[order]);
    backend->allocate_buffer(buffer, allocate);
    backend->set_debug_name(buffer, name);
    auto it = buffers.insert(std::move(buffer));
    if(!it) { return Handle<Buffer>{}; }
    if(*it == buffer_names.size()) { buffer_names.emplace_back(name); }
    else { buffer_names[*it] = name; }
    return Handle<Buffer>{ *it };
}

void Renderer::queue_destroy(Handle<Buffer>& buffer)
{
    ENG_ASSERT(buffer);
    current_data->retired_resources.push_back(FrameData::RetiredResource{ buffer, current_frame });
    buffer = {};
}

Handle<Image> Renderer::make_image(std::string_view name, Image&& image, AllocateMemory allocate, void* user_data)
{
    backend->allocate_image(image, allocate, user_data);
    backend->set_debug_name(image, name);
    auto it = images.insert(std::move(image));
    if(!it) { return Handle<Image>{}; }
    if(*it == image_names.size()) { image_names.emplace_back(name); }
    else { image_names[*it] = name; }
    return Handle<Image>{ *it };
}

void Renderer::queue_destroy(Handle<Image>& image, bool destroy_now)
{
    ENG_ASSERT(image);
    if(destroy_now)
    {
        backend->destroy_image(image.get());
        images.erase(Slotmap<Image, 1024>::SlotId{ *image });
    }
    else { current_data->retired_resources.push_back(FrameData::RetiredResource{ image, current_frame }); }
    image = {};
}

Handle<Sampler> Renderer::make_sampler(Sampler&& sampler)
{
    const auto found_handle = samplers.find(sampler);
    if(found_handle) { return found_handle; }
    backend->allocate_sampler(sampler);
    auto ret = samplers.insert(std::move(sampler));
    return ret.handle;
}

Handle<Shader> Renderer::make_shader(const std::filesystem::path& path)
{
    auto shader = Shader::init(path);
    const auto it = std::ranges::find_if(shaders, [&path](const auto& e) { return e.path == path; });
    const auto found_handle = it != shaders.end();
    if(!found_handle)
    {
        const auto handle = Handle<Shader>{ (Handle<Shader>::StorageType)shaders.size() };
        backend->make_shader(shader);
        shaders.push_back(std::move(shader));
        new_shaders.push_back(handle);
        return handle;
    }
    return Handle<Shader>{ (Handle<Shader>::StorageType)std::distance(shaders.begin(), it) };
}

Handle<DescriptorLayout> Renderer::make_layout(const DescriptorLayout& info)
{
    DescriptorLayout layout = info;
    backend->compile_layout(layout);
    dlayouts.push_back(std::move(layout));
    return Handle<DescriptorLayout>{ (uint32_t)dlayouts.size() - 1 };
}

Handle<PipelineLayout> Renderer::make_layout(const PipelineLayout& info)
{
    PipelineLayout layout = info;
    backend->compile_layout(layout);
    pplayouts.push_back(std::move(layout));
    return Handle<PipelineLayout>{ (uint32_t)pplayouts.size() - 1 };
}

Handle<Pipeline> Renderer::make_pipeline(const PipelineCreateInfo& info, Compilation compilation)
{
    Pipeline p{ .info = info, .type = PipelineType::NONE, .md = {} };
    if(!p.info.layout) { p.info.layout = settings.common_layout; }
    if(backend->caps.supports_bindless)
    {
        // with bindless, all pipeline layout should be the same
        ENG_ASSERT(p.info.layout == settings.common_layout);
    }
    const auto it = std::ranges::find(pipelines, p);
    const auto found_handle = it != pipelines.end();
    if(!found_handle)
    {
        const auto reuse_handle = false; // destroyed_pipelines.size();
        auto handle = Handle<Pipeline>{ (Handle<Pipeline>::StorageType)pipelines.size() };
        if(reuse_handle)
        {
            // handle = destroyed_pipelines.back();
            // destroyed_pipelines.pop_back();
        }
        backend->make_pipeline(p);
        if(compilation == Compilation::DEFERRED) { new_pipelines.push_back(handle); }
        else { compile_pipeline(p); }
        if(reuse_handle) { pipelines[*handle] = std::move(p); }
        else { pipelines.push_back(std::move(p)); }
        return handle;
    }
    return Handle<Pipeline>{ (Handle<Pipeline>::StorageType)std::distance(pipelines.begin(), it) };
}

void Renderer::destroy_pipeline(Handle<Pipeline> pipeline)
{
    ENG_ERROR("TODO: ADD TO DELETEION QUEUE");
    return;
    // if(!pipeline) { return; }
    // backend->destroy_pipeline(pipeline.get());
    // destroyed_pipelines.push_back(pipeline);
    // ENG_ASSERT(std::ranges::none_of(destroyed_pipelines, [pipeline](auto p) { return p == pipeline; }));
}

Sync* Renderer::make_sync(const SyncCreateInfo& info) { return backend->make_sync(info); }

void Renderer::destroy_sync(Sync* sync) { backend->destory_sync(sync); }

Handle<Material> Renderer::make_material(const Material& desc)
{
    Material mat = desc;
    if(!mat.mesh_pass) { mat.mesh_pass = settings.default_meshpass; }
    if(!mat.base_color_texture) { mat.base_color_texture = ImageView::init(settings.default_base_color); }

    auto ret = materials.insert(std::move(mat));
    if(ret.success) { new_materials.push_back(ret.handle); }
    return ret.handle;
}

Handle<Geometry> Renderer::make_geometry(const GeometryDescriptor& batch)
{
    ENG_ASSERT((batch.vertex_layout & ~(VertexComponent::POSITION_BIT | VertexComponent::NORMAL_BIT |
                                        VertexComponent::TANGENT_BIT | VertexComponent::UV0_BIT))
                   .empty());

    std::vector<float> out_vertices;
    std::vector<uint16_t> out_indices;
    std::vector<Meshlet> out_meshlets;
    meshletize_geometry(batch, out_vertices, out_indices, out_meshlets);

    const auto vertex_size = get_vertex_layout_size(batch.vertex_layout);
    const auto index_count = out_indices.size();
    const auto vertex_count = get_vertex_count(out_vertices, batch.vertex_layout);
    const auto pos_size = get_vertex_layout_size(VertexComponent::POSITION_BIT);
    const auto attr_size = vertex_size - pos_size;

    std::byte* vert_bytes = (std::byte*)out_vertices.data();
    std::vector<std::byte> positions(vertex_count * pos_size);
    std::vector<std::byte> attributes(vertex_count * attr_size);
    for(auto i = 0ull; i < vertex_count; ++i)
    {
        memcpy(&positions[i * pos_size], &vert_bytes[i * vertex_size], pos_size);
        memcpy(&attributes[i * attr_size], &vert_bytes[i * vertex_size + pos_size], attr_size);
    }
    std::vector<glm::vec4> bounding_spheres(out_meshlets.size());
    for(auto i = 0u; i < out_meshlets.size(); ++i)
    {
        out_meshlets.at(i).vertex_offset += bufs.vertex_count;
        out_meshlets.at(i).index_offset += bufs.index_count;
        bounding_spheres.at(i) = out_meshlets.at(i).bounding_sphere;
    }

    resize_buffer(bufs.positions, positions.size() * sizeof(positions[0]), STAGING_APPEND, true);
    resize_buffer(bufs.attributes, attributes.size() * sizeof(attributes[0]), STAGING_APPEND, true);
    resize_buffer(bufs.indices, out_indices.size() * sizeof(out_indices[0]), STAGING_APPEND, true);
    resize_buffer(bufs.bspheres, bounding_spheres.size() * sizeof(bounding_spheres[0]), STAGING_APPEND, true);
    staging->copy(bufs.positions.get(), positions, STAGING_APPEND);
    staging->copy(bufs.attributes.get(), attributes, STAGING_APPEND);
    staging->copy(bufs.indices.get(), out_indices, STAGING_APPEND);
    staging->copy(bufs.bspheres.get(), bounding_spheres, STAGING_APPEND);

    bufs.vertex_count += vertex_count;
    bufs.index_count += index_count;

    geometries.push_back(Geometry{ .meshlet_range = { (uint32_t)meshlets.size(), (uint32_t)out_meshlets.size() } });
    Handle<Geometry> handle{ (uint32_t)geometries.size() - 1 };

    meshlets.insert(meshlets.end(), out_meshlets.begin(), out_meshlets.end());

    ENG_LOG("Batching geometry: [VXS: {:.2f} KB, IXS: {:.2f} KB]",
            static_cast<float>(out_vertices.size() * sizeof(out_vertices[0])) / 1024.0f,
            static_cast<float>(out_indices.size() * sizeof(out_indices[0])) / 1024.0f);

    make_blas(handle);

    if(!handle) { return Handle<Geometry>{}; }
    return Handle<Geometry>{ *handle };
}

void Renderer::meshletize_geometry(const GeometryDescriptor& batch, std::vector<float>& out_vertices,
                                   std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets)
{
    static constexpr auto max_verts = 64u;
    static constexpr auto max_tris = 124u;
    static constexpr auto cone_weight = 0.0f;

    const auto& indices = batch.indices;

    const auto max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_verts, max_tris);
    std::vector<meshopt_Meshlet> mlts(max_meshlets);
    std::vector<uint32_t> mlt_vxs(max_meshlets * max_verts);
    std::vector<uint8_t> mlt_ids(max_meshlets * max_tris * 3);

    const auto vx_size = get_vertex_layout_size(batch.vertex_layout);
    const auto vx_count = get_vertex_count(batch.vertices, batch.vertex_layout);
    const auto mltcnt = meshopt_buildMeshlets(mlts.data(), mlt_vxs.data(), mlt_ids.data(), indices.data(), indices.size(),
                                              batch.vertices.data(), vx_count, vx_size, max_verts, max_tris, cone_weight);
    const auto& last_mlt = mlts.at(mltcnt - 1);
    mlt_vxs.resize(last_mlt.vertex_offset + last_mlt.vertex_count);
    mlt_ids.resize(last_mlt.triangle_offset + ((last_mlt.triangle_count * 3 + 3) & ~3));
    mlts.resize(mltcnt);

    for(auto& m : mlts)
    {
        meshopt_optimizeMeshlet(&mlt_vxs[m.vertex_offset], &mlt_ids[m.triangle_offset], m.triangle_count, m.vertex_count);
    }

    const std::vector<meshopt_Bounds> mlt_bnds =
        std::views::transform(mlts,
                              [&batch, &mlt_vxs, &mlt_ids, vx_count, vx_size](const meshopt_Meshlet& m) {
                                  return meshopt_computeMeshletBounds(&mlt_vxs[m.vertex_offset], &mlt_ids[m.triangle_offset],
                                                                      m.triangle_count, batch.vertices.data(), vx_count, vx_size);
                              }) |
        std::ranges::to<std::vector<meshopt_Bounds>>();
    out_vertices.resize(mlt_vxs.size() * vx_size / sizeof(float));
    for(auto i = 0ull; i < mlt_vxs.size(); ++i)
    {
        auto* pdst = (std::byte*)out_vertices.data();
        const auto* psrc = (const std::byte*)batch.vertices.data();
        memcpy(pdst + i * vx_size, psrc + mlt_vxs[i] * vx_size, vx_size);
    }

    out_indices =
        std::views::transform(mlt_ids, [](auto idx) { return (uint16_t)idx; }) | std::ranges::to<std::vector<uint16_t>>();
    out_meshlets =
        std::views::zip_transform(
            [](const meshopt_Meshlet& mlt, const meshopt_Bounds& mltb) {
                return Meshlet{ .vertex_offset = (int32_t)mlt.vertex_offset,
                                .vertex_count = mlt.vertex_count,
                                .index_offset = mlt.triangle_offset,
                                .index_count = mlt.triangle_count * 3,
                                .bounding_sphere = glm::vec4{ mltb.center[0], mltb.center[1], mltb.center[2], mltb.radius } };
            },
            mlts, mlt_bnds) |
        std::ranges::to<std::vector<Meshlet>>();
}

Handle<Mesh> Renderer::make_mesh(const MeshDescriptor& batch)
{
    Mesh mesh{ .geometry = batch.geometry, .material = batch.material };
    const auto found_it = std::find(meshes.begin(), meshes.end(), mesh);
    if(found_it != meshes.end()) { return Handle<Mesh>{ (uint32_t)std::distance(meshes.begin(), found_it) }; }
    const uint32_t idx = meshes.size();
    meshes.push_back(mesh);
    return Handle<Mesh>{ idx };
}

void Renderer::make_blas(Handle<Geometry> geom)
{
    if(!geom) { return; }
    new_blases.push_back(geom);
}

Handle<ShaderEffect> Renderer::make_shader_effect(const ShaderEffect& info)
{
    return shader_effects.insert(info).handle;
}

Handle<MeshPass> Renderer::make_mesh_pass(const MeshPass& info) { return mesh_passes.insert(info).handle; }

// Handle<MeshPass> Renderer::find_mesh_pass(std::string_view name)
//{
//     MeshPass mp{ .name = { name.begin(), name.end() }, .effects = {} };
//     return mesh_passes.find(mp);
// }

void Renderer::resize_buffer(Handle<Buffer>& handle, size_t new_size, bool copy_data)
{
    if(!handle)
    {
        ENG_ERROR("Buffer is null");
        return;
    }

    if(new_size <= handle->capacity)
    {
        handle->size = std::min(new_size, handle->size);
        return;
    }

    auto dsth = make_buffer(buffer_names[*handle], Buffer::init(new_size, handle->usage));
    if(copy_data) { staging->copy(dsth.get(), handle.get(), 0ull, { 0ull, std::min(new_size, handle->size) }, true); }
    queue_destroy(handle);
    handle = dsth;
}

void Renderer::resize_buffer(Handle<Buffer>& handle, size_t upload_size, size_t offset, bool copy_data)
{
    if(!handle)
    {
        ENG_ERROR("Buffer is null");
        return;
    }
    const auto& buf = handle.get();
    if(offset == STAGING_APPEND) { offset = buf.size; }
    const auto req_capacity = offset + upload_size;
    if(req_capacity <= buf.capacity) { return; }
    auto new_size = buf.capacity * 1.5;
    if(new_size < req_capacity) { new_size = req_capacity; }
    new_size = std::ceil(new_size);
    resize_buffer(handle, new_size, copy_data);
}

void Renderer::compile_pipeline(Pipeline& p)
{
    for(auto sh : p.info.shaders)
    {
        auto it = std::ranges::find(new_shaders, sh);
        if(it != new_shaders.end()) { backend->compile_shader(it->get()); }
        new_shaders.erase(it);
    }
    backend->compile_pipeline(p);
}

SubmitQueue* Renderer::get_queue(QueueType type) { return backend->get_queue(type); }

bool DescriptorLayout::is_compatible(const DescriptorLayout& a) const
{
    if(layout.size() != a.layout.size()) { return false; }
    for(size_t j = 0; j < layout.size(); ++j)
    {
        const auto& da = layout[j];
        const auto& db = a.layout[j];
        if(da.type != db.type) { return false; }
        if(da.slot != db.slot) { return false; }
        if(da.size != db.size) { return false; }
        if(da.stages != db.stages) { return false; }
        if(!da.immutable_samplers && !db.immutable_samplers) { /*empty*/ }
        else if(da.immutable_samplers != nullptr && db.immutable_samplers != nullptr)
        {
            for(auto i = 0u; i < da.size; ++i)
            {
                if(da.immutable_samplers[i] != db.immutable_samplers[i]) { return false; }
            }
        }
        else { return false; }
    }
    return true;
}

bool PipelineLayout::is_compatible(const PipelineLayout& a) const
{
    if(push_range != a.push_range) { return false; }

    const size_t set_count = std::min(layout.size(), a.layout.size());
    for(size_t i = 0; i < set_count; ++i)
    {
        const auto& s1 = layout[i];
        const auto& s2 = a.layout[i];
        if(!s1->is_compatible(s2.get())) { return false; }
    }
    return true;
}

PipelineCreateInfo PipelineCreateInfo::init(const std::vector<fs::Path>& shaders, Handle<PipelineLayout> layout)
{
    PipelineCreateInfo info{};
    info.shaders = shaders | std::views::transform([](const fs::Path& path) { return get_renderer().make_shader(path); }) |
                   std::ranges::to<std::vector>();
    info.layout = layout;
    return info;
}

// todo: swapchain impl should not be here
uint32_t Swapchain::acquire(uint64_t timeout, Sync* semaphore, Sync* fence)
{
    current_index = acquire_impl(this, timeout, semaphore, fence);
    return current_index;
}

Handle<Image> Swapchain::get_image() const { return images.at(current_index); }

ImageView Swapchain::get_view() const { return views.at(current_index); }

float TimestampQuery::to_ms(const TimestampQuery& q)
{
    uint64_t results[2];
    get_renderer().backend->get_query_pool_results(q.pool, q.index, 2, &results);
    return (float)(((double)(results[1] - results[0])) * get_renderer().backend->limits.timestampPeriodNs * 1e-6);
}

ScopedTimestampQuery::ScopedTimestampQuery(std::string_view label, ICommandBuffer* cmd) : cmd(cmd)
{
    auto& r = get_renderer();
    auto& cd = r.current_data;
    auto& tq = cd->timestamp_queries.emplace_back();
    query = &tq;
    query->label = label;
    query->pool = cd->timestamp_pool;
    query->index = query->pool->allocate_queries(2);
    cmd->reset_query_indices(query->pool, query->index, 2);
    cmd->write_timestamp(query->pool, PipelineStage::ALL, query->index);
}

ScopedTimestampQuery::~ScopedTimestampQuery()
{
    cmd->write_timestamp(query->pool, PipelineStage::ALL, query->index + 1);
}

void Renderer::DebugGeomBuffers::render(CommandBufferVk* cmd, Sync* s)
{
    ENG_ASSERT(s == nullptr);
    if(geometry.empty()) { return; }
    const auto verts = expand_into_vertices();
    ENG_ASSERT(verts.size() > geometry.size() && verts.size() % 2 == 0);
    if(!vpos_buf)
    {
        vpos_buf = get_engine().renderer->make_buffer("debug verts", Buffer::init(verts.size() * sizeof(verts[0]),
                                                                                  BufferUsage::STORAGE_BIT));
    }

    ENG_ASSERT(false);
    // get_engine().renderer->sbuf->copy(vpos_buf, verts, 0);
    // get_engine().renderer->sbuf->flush()->wait_cpu(~0ull);

    ENG_ASSERT(false);
    // cmd->bind_resource(1, vpos_buf);
    cmd->draw(verts.size(), 1, 0, 0);
    geometry.clear();
}

std::vector<glm::vec3> Renderer::DebugGeomBuffers::expand_into_vertices()
{
    const auto num_verts = std::transform_reduce(geometry.begin(), geometry.end(), 0ull, std::plus<>{}, [](auto val) {
        // NONE, AABB,
        static constexpr uint32_t NUM_VERTS[]{ 0u, 24u };
        return NUM_VERTS[std::to_underlying(val.type)];
    });
    std::vector<glm::vec3> verts;
    verts.reserve(num_verts);
    const auto push_line = [&verts](auto a, auto b) {
        verts.push_back(a);
        verts.push_back(b);
    };
    for(const auto& e : geometry)
    {
        switch(e.type)
        {
        case DebugGeometry::Type::AABB:
        {
            const glm::vec3& min = e.data.aabb.a;
            const glm::vec3& max = e.data.aabb.b;

            // 8 corners
            const glm::vec3 v000{ min.x, min.y, min.z };
            const glm::vec3 v100{ max.x, min.y, min.z };
            const glm::vec3 v010{ min.x, max.y, min.z };
            const glm::vec3 v110{ max.x, max.y, min.z };

            const glm::vec3 v001{ min.x, min.y, max.z };
            const glm::vec3 v101{ max.x, min.y, max.z };
            const glm::vec3 v011{ min.x, max.y, max.z };
            const glm::vec3 v111{ max.x, max.y, max.z };

            push_line(v000, v100);
            push_line(v100, v110);
            push_line(v110, v010);
            push_line(v010, v000);

            push_line(v001, v101);
            push_line(v101, v111);
            push_line(v111, v011);
            push_line(v011, v001);

            push_line(v000, v001);
            push_line(v100, v101);
            push_line(v110, v111);
            push_line(v010, v011);
            break;
        }
        default:
        {
            ENG_ERROR("Unhandled case");
            continue;
        }
        }
    }
    return verts;
}

ImageView ImageView::init(Handle<Image> image, std::optional<ImageFormat> format, std::optional<ImageViewType> type,
                          uint32_t src_mip, uint32_t dst_mip, uint32_t src_layer, uint32_t dst_layer)
{
    ENG_ASSERT(image);
    Image& img = image.get();
    if(!format) { format = img.format; }
    if(!type) { type = get_view_type_from_image(img.type); }
    if(dst_mip == ~0u) { dst_mip = img.mips - 1; }
    if(dst_layer == ~0u) { dst_layer = img.layers - 1; }
    ImageView view{ .image = image,
                    .type = *type,
                    .format = *format,
                    .src_subresource = img.mips * src_layer + src_mip,
                    .dst_subresource = img.mips * dst_layer + dst_mip };
    return view;
}

ImageView::Metadata ImageView::get_md() const { return get_renderer().backend->get_md(*this); }

} // namespace gfx
} // namespace eng
