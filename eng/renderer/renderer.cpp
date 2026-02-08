#include <meshoptimizer/src/meshoptimizer.h>
#include "renderer.hpp"
#include <eng/renderer/staging_buffer.hpp>
#include <eng/engine.hpp>
#include <eng/camera.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/common/to_vk.hpp>
#include <eng/common/to_string.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/common/paths.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/ecs/components.hpp>
#include <eng/ui.hpp>
#include <eng/renderer/passes/passes.hpp>
#include <eng/scene.hpp>
#include <assets/shaders/bindless_structures.glsli>

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
    swapchain = backend->make_swapchain();
    sbuf = new StagingBuffer{};
    sbuf->init(gq);
    rgraph = new RenderGraph{};
    rgraph->init(this);

    init_bufs();
    init_perframes();
    init_pipelines();
    init_helper_geom();
    init_rgraph_passes();

    imgui_renderer = new ImGuiRenderer{};
    imgui_renderer->init();

    ecs_mesh_view = Engine::get().ecs->get_view<ecs::Transform, ecs::Mesh>([this](ecs::entity e) { instance_entity(e); },
                                                                           [this](ecs::entity e, ecs::signature sig) {
                                                                               if(sig.test(ecs::get_id<ecs::Transform>()))
                                                                               {
                                                                                   new_transforms.push_back(e);
                                                                               }
                                                                               if(sig.test(ecs::get_id<ecs::Mesh>()))
                                                                               {
                                                                                   ENG_TODO();
                                                                                   ENG_ASSERT(false);
                                                                               }
                                                                           });
    ecs_light_view = Engine::get().ecs->get_view<ecs::Light>(
        [this](ecs::entity e) {
            new_lights.push_back(e);
            ++bufs.light_count;
        },
        [this](ecs::entity e, ecs::signature sig) { new_lights.push_back(e); });

    // Engine::get().ui->add_tab(UI::Tab{
    //     "Debug",
    //     UI::Location::RIGHT_PANE,
    //     [this] {
    //         if(!Engine::get().ui->show_debug_tab) { return; }
    //         if(ImGui::Begin("Debug"))
    //         {
    //             auto* camera = Engine::get().camera;
    //             ImGui::SeparatorText("Camera");
    //             ImGui::Text("Position: %.2f %.2f %.2f", camera->pos.x, camera->pos.y, camera->pos.z);

    //            ImGui::SeparatorText("Forward+");
    //            ImGui::Text("Tile size: %u px", bufs.fwdp_tile_pixels);
    //            ImGui::Text("Num tiles: %u", bufs.fwdp_num_tiles);
    //            ImGui::Text("Lights per tile: %u", bufs.fwdp_lights_per_tile);

    //            bool fwdp_grid_output = debug_output == DebugOutput::FWDP_GRID;
    //            bool changed = false;
    //            changed |= ImGui::Checkbox("FWDP heatmap", &fwdp_grid_output);
    //            if(fwdp_grid_output) { debug_output = DebugOutput::FWDP_GRID; }
    //            else { debug_output = DebugOutput::COLOR; }
    //            ImGui::Checkbox("FWDP enable", &fwdp_enable);

    //            ImGui::SeparatorText("Culling");
    //            const auto& ppf = get_perframe(-1);
    //            const auto& cullbuf =
    //            rgraph->get_resource(rgraphpasses->cull_zprepass->culled_id_bufs.get(-1)).buffer.get(); uint32_t mlts
    //            = 0; for(auto [e, t, m] : ecs_mesh_view)
    //            {
    //                const auto& msh = Engine::get().ecs->get<ecs::Mesh>(e);
    //                for(auto mmsh : msh->meshes)
    //                {
    //                    mlts += mmsh->geometry->meshlet_range.size;
    //                }
    //            }
    //            ImGui::Text("Drawn meshlets: %u", *(uint32_t*)cullbuf.memory);
    //            float culled_percent = (float)(*(uint32_t*)cullbuf.memory) / mlts;
    //            ImGui::Text("%.2f%% of meshlets culled", 100.0f - culled_percent * 100.0f);
    //            ImGui::Checkbox("Meshlet frustum culling", &mlt_frust_cull_enable);
    //            ImGui::Checkbox("Meshlet occlusion culling", &mlt_occ_cull_enable);
    //        }
    //        ImGui::End();
    //    },
    //});
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
    // helpergeom.ppskybox = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { Engine::get().renderer->make_shader("common/skybox.vert.glsl"),
    //                  Engine::get().renderer->make_shader("common/skybox.frag.glsl") },
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
    auto linear_sampler = make_sampler(SamplerDescriptor{});
    auto nearest_sampler = make_sampler(SamplerDescriptor{ .filtering = { ImageFilter::NEAREST, ImageFilter::NEAREST } });
    auto hiz_sampler = make_sampler(SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE },
        .mipmap_mode = SamplerMipmapMode::NEAREST,
        .reduction_mode = SamplerReductionMode::MIN });

    Handle<Sampler> imsamplers[3]{};
    imsamplers[ENG_SAMPLER_LINEAR] = linear_sampler;
    imsamplers[ENG_SAMPLER_NEAREST] = nearest_sampler;
    imsamplers[ENG_SAMPLER_HIZ] = hiz_sampler;

    if(backend->caps.supports_bindless)
    {
        const auto common_dlayout = make_layout(DescriptorLayout{
            .layout = {
                { DescriptorType::STORAGE_BUFFER, BINDLESS_STORAGE_BUFFER_BINDING, 1024, ShaderStage::ALL },
                { DescriptorType::STORAGE_IMAGE, BINDLESS_STORAGE_IMAGE_BINDING, 1024, ShaderStage::ALL },
                { DescriptorType::SAMPLED_IMAGE, BINDLESS_SAMPLED_IMAGE_BINDING, 1024, ShaderStage::ALL },
                { DescriptorType::SEPARATE_SAMPLER, BINDLESS_SAMPLER_BINDING, std::size(imsamplers), ShaderStage::ALL, imsamplers },
            } });
        common_playout = make_layout(PipelineLayout{
            .layout = { common_dlayout },
            .push_range = { ShaderStage::ALL, PushRange::MAX_PUSH_BYTES },
        });
        descriptor_allocator = new DescriptorSetAllocatorBindlessVk{ common_playout.get() };
    }
    else
    {
        ENG_ERROR("Nonbindless path not supported.");
        return;
    }

    // hiz_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { Engine::get().renderer->make_shader("culling/hiz.comp.glsl") }, .layout = common_playout });
    // cull_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { Engine::get().renderer->make_shader("culling/culling.comp.glsl") },
    //     .layout = common_playout,
    // });
    // cullzout_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { Engine::get().renderer->make_shader("common/zoutput.vert.glsl"),
    //                  Engine::get().renderer->make_shader("common/zoutput.frag.glsl") },
    //     .layout = common_playout,
    //     .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
    //     .depth_test = true,
    //     .depth_write = true,
    //     .depth_compare = DepthCompare::GREATER,
    //     .culling = CullFace::BACK,
    // });
    // fwdp_cull_lights_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
    //     .shaders = { Engine::get().renderer->make_shader("forwardp/cull_lights.comp.glsl") },
    //     .layout = common_playout,
    // });
    // default_unlit_pipeline = make_pipeline(PipelineCreateInfo{
    //     .shaders = { make_shader("default_unlit/unlit.vert.glsl"), make_shader("default_unlit/unlit.frag.glsl") },
    //     .layout = common_playout,
    //     .attachments = { .count = 1,
    //                      .color_formats = { ImageFormat::R8G8B8A8_SRGB },
    //                      .blend_states = { PipelineCreateInfo::BlendState{ .enable = true,
    //                                                                        .src_color_factor = BlendFactor::SRC_ALPHA,
    //                                                                        .dst_color_factor = BlendFactor::ONE_MINUS_SRC_ALPHA,
    //                                                                        .color_op = BlendOp::ADD,
    //                                                                        .src_alpha_factor = BlendFactor::ONE,
    //                                                                        .dst_alpha_factor = BlendFactor::ZERO,
    //                                                                        .alpha_op = BlendOp::ADD } },
    //                      .depth_format = ImageFormat::D32_SFLOAT },
    //     .depth_test = true,
    //     .depth_write = false,
    //     .depth_compare = DepthCompare::GEQUAL,
    //     .culling = CullFace::BACK,
    // });

    default_meshpass =
        make_mesh_pass(MeshPass::init("default_unlit").set(RenderPassType::FORWARD, make_shader_effect(ShaderEffect{ .pipeline = default_unlit_pipeline })));
    default_material = materials.insert(Material{ .mesh_pass = default_meshpass }).handle;
}

void Renderer::init_perframes()
{
    auto* ew = Engine::get().window;
    perframe.resize(frames_in_flight);
    for(auto i = 0u; i < frames_in_flight; ++i)
    {
        auto& pf = perframe[i];
        // auto& gb = pf.gbuffer;
        // gb.color = make_image(ImageDescriptor{
        //     .name = ENG_FMT("gcolor{}", i),
        //     .width = (uint32_t)ew->width,
        //     .height = (uint32_t)ew->height,
        //     .format = ImageFormat::R8G8B8A8_SRGB,
        //     .usage = ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::TRANSFER_RW | ImageUsage::SAMPLED_BIT,
        // });
        // gb.depth = make_image(ImageDescriptor{
        //     .name = ENG_FMT("gdepth{}", i),
        //     .width = (uint32_t)ew->width,
        //     .height = (uint32_t)ew->height,
        //     .format = ImageFormat::D32_SFLOAT,
        //     .usage = ImageUsage::DEPTH_BIT | ImageUsage::TRANSFER_RW | ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT,
        // });
        pf.cmdpool = gq->make_command_pool();
        pf.acq_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("acquire semaphore {}", i) });
        pf.ren_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("rendering semaphore {}", i) });
        pf.ren_fen = make_sync({ SyncType::FENCE, 1, ENG_FMT("rendering fence {}", i) });
        pf.swp_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 1, ENG_FMT("swap semaphore {}", i) });
        pf.constants =
            make_buffer(Buffer::init(ENG_FMT("constants_{}", i), sizeof(GPUEngConstantsBuffer), BufferUsage::STORAGE_BIT));
    }
}

void Renderer::init_bufs()
{
    bufs.positions = make_buffer(Buffer::init("vertex positions", 1024, BufferUsage::STORAGE_BIT));
    bufs.attributes = make_buffer(Buffer::init("vertex attributes", 1024, BufferUsage::STORAGE_BIT));
    bufs.indices = make_buffer(Buffer::init("vertex indices", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDEX_BIT));
    bufs.bspheres = make_buffer(Buffer::init("bounding spheres", 1024, BufferUsage::STORAGE_BIT));
    bufs.materials = make_buffer(Buffer::init("materials", 1024, BufferUsage::STORAGE_BIT));
    for(uint32_t i = 0; i < 2; ++i)
    {
        bufs.transforms[i] = make_buffer(Buffer::init(ENG_FMT("trs {}", i), 1024, BufferUsage::STORAGE_BIT));
        bufs.lights[i] = make_buffer(Buffer::init(ENG_FMT("lights {}", i), 1024, BufferUsage::STORAGE_BIT));
    }
    {
        const auto* w = Engine::get().window;
        const auto num_tiles_x = (uint32_t)std::ceilf(w->width / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles_y = (uint32_t)std::ceilf(w->height / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles = num_tiles_x * num_tiles_y;
        bufs.fwdp_num_tiles = num_tiles;
    }
}

void Renderer::init_rgraph_passes()
{
    ENG_ASSERT(rgraph_passes.empty());
    rgraph_passes.push_back(new pass::SSTriangle{});

    for(auto& pass : rgraph_passes)
    {
        pass->init();
    }

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
}

void Renderer::instance_entity(ecs::entity e)
{
    if(!Engine::get().ecs->has<ecs::Transform, ecs::Mesh>(e))
    {
        ENG_WARN("Entity {} does not have the required components (Transform, Mesh).", e);
        return;
    }
    auto* mesh = Engine::get().ecs->get<ecs::Mesh>(e);
    if(mesh->gpu_resource != ~0u)
    {
        ENG_WARN("Entity {} with mesh {} is already instanced {}", e, mesh->asset->name, mesh->gpu_resource);
        return;
    }
    new_transforms.push_back(e);
    ++bufs.transform_count;
    mesh->gpu_resource = gpu_resource_allocator.allocate_slot();
    for(const auto& rmesh : mesh->asset->render_meshes)
    {
        const auto& mpeffect = rmesh->material->mesh_pass->effects;
        for(auto i = 0u; i < mpeffect.size(); ++i)
        {
            if(mpeffect.at(i)) { render_passes.get((RenderPassType)i).entities.push_back(e); }
        }
    }
}

void Renderer::update()
{
    auto* ew = Engine::get().window;
    auto& pf = get_framedata();
    const auto& ppf = perframe.at(frame_index % perframe.size()); // get previous frame res

    pf.ren_fen->wait_cpu(~0ull);
    pf.ren_fen->reset();
    pf.acq_sem->reset();
    pf.ren_sem->reset();
    pf.swp_sem->reset();
    pf.cmdpool->reset();

    sbuf->reset();
    sbuf->next();
    sbuf->reset();
    swapchain->acquire(~0ull, pf.acq_sem);

    if(pf.retired_resources.size() > 0)
    {
        ENG_LOG("Removing {} retired resources", pf.retired_resources.size());
        for(auto& rs : pf.retired_resources)
        {
            if(auto* buf = std::get_if<Handle<Buffer>>(&rs))
            {
                backend->destroy_buffer(buf->get());
                buffers.erase(*buf);
            }
            else if(auto* img = std::get_if<Handle<Image>>(&rs))
            {
                backend->destroy_image(img->get());
                images.erase(*img);
            }
        }
        pf.retired_resources.clear();
    }

    build_renderpasses();

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
        for(auto& e : new_pipelines)
        {
            backend->compile_pipeline(e.get());
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
                GPUMaterial gpumat{ .base_color_idx = descriptor_allocator->get_bindless(e->base_color_texture, false) };
                sbuf->copy(bufs.materials, &gpumat, *e * sizeof(gpumat), sizeof(gpumat));
            }
            else { ENG_ASSERT(false); }
        }
        new_materials.clear();
    }
    if(new_transforms.size())
    {
        std::swap(bufs.transforms[0], bufs.transforms[1]);
        const auto req_size = bufs.transform_count * sizeof(glm::mat4);
        resize_buffer(bufs.transforms[0], req_size, false);
        sbuf->copy(bufs.transforms[0], bufs.transforms[1], 0, { 0, bufs.transforms[1]->size }, true);
        for(auto i = 0u; i < new_transforms.size(); ++i)
        {
            const auto* trs = Engine::get().ecs->get<ecs::Transform>(new_transforms[i]);
            const auto* msh = Engine::get().ecs->get<ecs::Mesh>(new_transforms[i]);
            sbuf->copy(bufs.transforms[0], &trs->global, msh->gpu_resource * sizeof(trs->global), sizeof(trs->global));
        }
        new_transforms.clear();
    }
    if(new_lights.size())
    {
        std::swap(bufs.lights[0], bufs.lights[1]);
        const auto req_size = bufs.light_count * sizeof(GPULight) + 4;
        resize_buffer(bufs.lights[0], req_size, false);
        sbuf->copy(bufs.lights[0], bufs.lights[1], 0, { 0, bufs.lights[1]->size }, true);
        for(auto i = 0u; i < new_lights.size(); ++i)
        {
            auto* l = Engine::get().ecs->get<ecs::Light>(new_lights[i]);
            const auto* t = Engine::get().ecs->get<ecs::Transform>(new_lights[i]);
            if(l->gpu_index == ~0u) { l->gpu_index = gpu_light_allocator.allocate_slot(); }
            GPULight gpul{ t->pos(), l->range, l->color, l->intensity, (uint32_t)l->type };
            sbuf->copy(bufs.lights[0], &gpul, offsetof(GPULightsBuffer, lights_us) + l->gpu_index * sizeof(GPULight),
                       sizeof(GPULight));
        }
        const auto lc = (uint32_t)ecs_light_view.size();
        sbuf->copy(bufs.lights[0], &lc, offsetof(GPULightsBuffer, count), 4);
        new_lights.clear();
    }

    const auto view = Engine::get().camera->get_view();
    const auto proj = Engine::get().camera->get_projection();
    const auto invview = glm::inverse(view);
    const auto invproj = glm::inverse(proj);

    static auto prev_view = view;
    if(true || glfwGetKey(Engine::get().window->window, GLFW_KEY_EQUAL) == GLFW_PRESS) { prev_view = view; }

    // ENG_ASSERT(false);
    //  GPUEngConstantsBuffer cb{
    //      .vposb = get_bindless(bufs.vpos_buf),
    //      .vatrb = get_bindless(bufs.vattr_buf),
    //      .vidxb = get_bindless(bufs.idx_buf),
    //      .GPUBoundingSpheresBufferIndex = get_bindless(bufs.bsphere_buf),
    //      .GPUTransformsBufferIndex = get_bindless(bufs.trs_bufs[0]),
    //      .rmatb = get_bindless(bufs.mats_buf),
    //      .GPULightsBufferIndex = get_bindless(bufs.lights_bufs[0]),
    //      .view = view,
    //      .prev_view = prev_view,
    //      .proj = proj,
    //      .proj_view = proj * view,
    //      .inv_view = invview,
    //      .inv_proj = invproj,
    //      .inv_proj_view = invview * invproj,
    //      //.rand_mat = rand_mat,
    //      .cam_pos = Engine::get().camera->pos,

    //    .output_mode = (uint32_t)debug_output,
    //    .fwdp_enable = (uint32_t)fwdp_enable,
    //    .fwdp_max_lights_per_tile = bufs.fwdp_lights_per_tile,
    //    .mlt_frust_cull_enable = (uint32_t)mlt_frust_cull_enable,
    //    .mlt_occ_cull_enable = (uint32_t)mlt_occ_cull_enable,
    //};
    // sbuf->copy(pf.constants, &cb, 0ull, sizeof(cb));

    // ENG_ASSERT(false);
    //  rgraph->add_pass(&*rgraphpasses->cull_zprepass);
    //  rgraph->add_pass(&*rgraphpasses->cull_hiz);
    //  rgraph->add_pass(&*rgraphpasses->cull_main);
    //  rgraph->add_pass(&*rgraphpasses->fwdp_lightcull);
    //  rgraph->add_pass(&*rgraphpasses->default_unlit);
    //  rgraph->add_pass(&*rgraphpasses->debug_geom);
    //  rgraph->add_pass(&*rgraphpasses->imgui);
    //  rgraph->add_pass(&*rgraphpasses->present_copy);
    //  rgraph->compile();
    //  auto* rgsync = rgraph->execute(sbuf->flush());

    // auto* cmd = pf.cmdpool->begin();
    // cmd->barrier(swapchain->get_image().get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::ALL,
    //              PipelineAccess::NONE, ImageLayout::TRANSFER_DST, ImageLayout::PRESENT);
    // pf.cmdpool->end(cmd);

    for(auto& pass : rgraph_passes)
    {
        pass->on_render_graph(*rgraph);
    }
    rgraph->compile();

    // sbuf->queue_wait(rgraph->gq, PipelineStage::ALL, true);

    auto* cmd = pf.cmdpool->begin();
    cmd->barrier(swapchain->get_image().get(), PipelineStage::ALL, PipelineAccess::TRANSFER_WRITE_BIT,
                 PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::TRANSFER_DST, ImageLayout::PRESENT);
    pf.cmdpool->end(cmd);

    sbuf->reset();
    Sync* rgsync = rgraph->execute(pf.acq_sem);

    gq->wait_sync(rgsync, PipelineStage::ALL)
        .with_cmd_buf(cmd)
        .signal_sync(pf.swp_sem, PipelineStage::ALL)
        .signal_sync(pf.ren_fen)
        .submit();
    gq->wait_sync(pf.swp_sem, PipelineStage::ALL).present(swapchain);
    gq->wait_idle();
    ++frame_index;
}

void Renderer::render(RenderPassType pass, SubmitQueue* queue, CommandBufferVk* cmd)
{
    // ENG_ASSERT(false);
    //  auto* ew = Engine::get().window;
    //  auto& pf = get_perframe();
    //  auto& rp = render_passes.at(pass);

    // const VkRenderingAttachmentInfo vkcols[] = { Vks(VkRenderingAttachmentInfo{
    //     .imageView = pf.gbuffer.color->default_view->md.vk->view,
    //     .imageLayout = to_vk(ImageLayout::ATTACHMENT),
    //     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    //     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    //     .clearValue = { .color = { .uint32 = {} } } }) };
    // const auto vkdep = Vks(VkRenderingAttachmentInfo{ .imageView = pf.gbuffer.depth->default_view->md.vk->view,
    //                                                   .imageLayout = to_vk(ImageLayout::ATTACHMENT),
    //                                                   .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    //                                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE });
    // VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
    // VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
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
    //     cmd->bind_resource(0, pf.constants);
    //     cmd->bind_resource(1, ib.ids_buf);
    //     const auto& fwd = rgraph->get_pass<pass::fwdp::LightCulling>("fwdp::LightCulling");
    //     cmd->bind_resource(2, rgraph->get_resource(fwd.culled_light_grid_bufs).buffer);
    //     cmd->bind_resource(3, rgraph->get_resource(fwd.culled_light_list_bufs).buffer);
    // });
    // cmd->end_rendering();
}

void Renderer::build_renderpasses()
{
    for(auto rpti = 0u; rpti < (int)RenderPassType::LAST_ENUM; ++rpti)
    {
        auto& rp = render_passes.passes[rpti];
        if(!rp.needs_rebuild()) { continue; }

        rp.clear();
        for(const auto& e : rp.entities)
        {
            auto* ecsmesh = Engine::get().ecs->get<ecs::Mesh>(e);
            if(!ecsmesh)
            {
                ENG_ERROR("Entity {} does not have a mesh", e);
                continue;
            }
            if(ecsmesh->gpu_resource == ~0u)
            {
                ENG_ERROR("Entity {} was not instanced properly. Forgot to call instance_entity()?", e);
                continue;
            }

            for(const auto& e : ecsmesh->asset->render_meshes)
            {
                const auto& mesh = e.get();
                const auto& geom = mesh.geometry.get();
                for(auto i = 0u; i < geom.meshlet_range.size; ++i)
                {
                    rp.mesh_instances.push_back(MeshInstance{ mesh.geometry, mesh.material, ecsmesh->gpu_resource,
                                                              geom.meshlet_range.offset + i });
                }
            }
        }

        std::sort(rp.mesh_instances.begin(), rp.mesh_instances.end(), [](const MeshInstance& a, const MeshInstance& b) {
            if(a.material < b.material && a.meshlet_index < b.meshlet_index) { return true; }
            return false;
        });

        std::vector<DrawIndexedIndirectCommand> commands; // command for each geometry
        commands.reserve(rp.mesh_instances.size());
        std::vector<GPUInstanceId> instance_indices; // instanceoffset + instance id is the index of the current instance being renderd; accessed from shaders
        instance_indices.reserve(rp.mesh_instances.size());
        std::vector<uint32_t> counts; // how many commands can be drawn without changing the pipeline
        counts.reserve(rp.mesh_instances.size());
        rp.draw.batches.reserve(rp.mesh_instances.size());
        for(auto i = 0u; i < rp.mesh_instances.size(); ++i)
        {
            const auto& geom = rp.mesh_instances[i].geometry.get();
            const auto& material = rp.mesh_instances[i].material.get();
            const auto& mp = material.mesh_pass->effects[rpti].get();
            if(i == 0 || rp.draw.batches.back().pipeline != mp.pipeline)
            {
                rp.draw.batches.push_back(InstanceBatch{ mp.pipeline, 0, 0 });
                counts.push_back(0);
            }
            if(i == 0 || rp.mesh_instances[i - 1].meshlet_index != rp.mesh_instances[i].meshlet_index)
            {
                const auto& mlt = meshlets[rp.mesh_instances[i].meshlet_index];
                commands.push_back(DrawIndexedIndirectCommand{ .indexCount = mlt.index_count,
                                                               .instanceCount = 0,
                                                               .firstIndex = mlt.index_offset,
                                                               .vertexOffset = mlt.vertex_offset,
                                                               .firstInstance = i });
            }
            ++rp.draw.batches.back().instance_count;
            rp.draw.batches.back().command_count =
                commands.size() - (rp.draw.batches.size() > 1 ? 0ull : rp.draw.batches[rp.draw.batches.size() - 2].command_count);
            ++commands.back().instanceCount;
            counts.back() = rp.draw.batches.back().command_count;
        }

        const auto counts_size = counts.size() * sizeof(counts[0]);
        const auto cmds_start = align_up2(counts_size, 16);
        const auto cmds_size = commands.size() * sizeof(backend->get_indirect_indexed_command_size());
        const auto total_size = cmds_start + cmds_size;

        if(!rp.draw.indirect_buf)
        {
            rp.draw.indirect_buf = make_buffer(Buffer::init(ENG_FMT("{} indirect buffer", to_string((RenderPassType)rpti)),
                                                            total_size, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT));
        }
        if(!rp.instance_buffer)
        {
            rp.instance_buffer =
                make_buffer(Buffer::init(ENG_FMT("{} instance buffer", to_string((RenderPassType)rpti)),
                                         instance_indices.size() * sizeof(instance_indices[0]) + 4, BufferUsage::STORAGE_BIT));
        }

        std::vector<std::byte> command_bytes(cmds_size);
        for(auto i = 0u; i < commands.size(); ++i)
        {
            backend->make_indirect_indexed_command(&command_bytes[i * backend->get_indirect_indexed_command_size()],
                                                   commands[i].indexCount, commands[i].instanceCount, commands[i].firstIndex,
                                                   commands[i].vertexOffset, commands[i].firstInstance);
        }

        resize_buffer(rp.draw.indirect_buf, total_size, false);
        resize_buffer(rp.instance_buffer, instance_indices.size() * sizeof(instance_indices[0]), false);
        sbuf->copy(rp.draw.indirect_buf, counts, 0ull);
        sbuf->copy(rp.draw.indirect_buf, command_bytes, cmds_start);
        sbuf->copy_value(rp.instance_buffer, (uint32_t)instance_indices.size(), offsetof(GPUInstanceIdsBuffer, count));
        sbuf->copy(rp.instance_buffer, instance_indices, offsetof(GPUInstanceIdsBuffer, ids_us));
        rp.draw.counts_view = BufferView{ rp.draw.indirect_buf, { 0ull, counts_size } };
        rp.draw.cmds_view = BufferView{ rp.draw.indirect_buf, { cmds_start, cmds_size } };
        rp.instance_view = BufferView{ rp.instance_buffer, { 0ull, ~0ull } };
    }
}

void Renderer::render_debug(const DebugGeometry& geom) { debug_bufs.add(geom); }

Handle<Buffer> Renderer::make_buffer(Buffer&& buffer, std::optional<dont_alloc_tag> dont_alloc)
{
    uint32_t order = 0;
    float size = (float)buffer.capacity;
    static constexpr const char* units[]{ "B", "KB", "MB", "GB" };
    for(; size >= 1024.0f && order < std::size(units); size /= 1024.0f, ++order) {}
    ENG_LOG("Creating buffer {} [{:.2f} {}]", buffer.name, size, units[order]);
    backend->allocate_buffer(buffer, dont_alloc);
    return buffers.insert(std::move(buffer));
}

void Renderer::destroy_buffer(Handle<Buffer>& buffer)
{
    ENG_ASSERT(buffer);
    get_framedata().retired_resources.push_back(buffer);
    buffer = {};
}

Handle<Image> Renderer::make_image(Image&& image, std::optional<dont_alloc_tag> dont_alloc)
{
    backend->allocate_image(image, dont_alloc);
    return images.insert(std::move(image));

    // sbuf -> copy()
    //  auto h = images.insert(backend->make_image(info));
    //  h->default_view = make_view(ImageViewDescriptor{ .name = ENG_FMT("{}_default", info.name), .image = h });
    //  if(info.data.size_bytes())
    //{
    //      sbuf->copy(h, info.data.data(), false);
    //      for(auto i = 0u; i < info.mips - 1; ++i)
    //      {
    //          const Range3D32i srcsz{
    //              { 0, 0, 0 }, { std::max(info.width >> i, 1u), std::max(info.height >> i, 1u), std::max(info.depth >> i, 1u) }
    //          };
    //          const Range3D32i dstsz{ { 0, 0, 0 },
    //                                  { std::max(srcsz.size.x >> 1, 1), std::max(srcsz.size.y >> 1, 1),
    //                                    std::max(srcsz.size.z >> 1, 1) } };
    //          sbuf->barrier(h, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC, ImageSubRange{ { i, 1 }, { 0, 1 } });
    //          sbuf->blit(h, h, ImageBlit{ { i, { 0, 1 } }, { i + 1, { 0, 1 } }, srcsz, dstsz });
    //          sbuf->barrier(h, ImageLayout::TRANSFER_SRC, ImageLayout::READ_ONLY, ImageSubRange{ { i, 1 }, { 0, 1 } });
    //      }
    //      sbuf->barrier(h, ImageLayout::TRANSFER_DST, ImageLayout::READ_ONLY, ImageSubRange{ { info.mips - 1, 1 }, { 0, 1 } });
    //  }
    //  return h;
}

void Renderer::destroy_image(Handle<Image>& image)
{
    ENG_ASSERT(image);
    get_framedata().retired_resources.push_back(image);
    image = {};
}

// Handle<BufferView> Renderer::make_view(const BufferViewDescriptor& info)
//{
//     auto& res = Handle{ info.buffer }.get();
//     const auto ret = buffer_views.insert(BufferView{ .buffer = info.buffer, .range = info.range });
//     return ret.handle;
// }

// ImageView Renderer::make_view(const ImageViewDescriptor& info)
//{
//     auto& img = Handle{ info.image }.get();
//     auto view = ImageView{ .name = info.name,
//                            .image = info.image,
//                            .type = info.view_type ? *info.view_type : img.deduce_view_type(),
//                            .format = info.format ? *info.format : img.format,
//                            .aspect = info.aspect ? *info.aspect : img.deduce_aspect(),
//                            .mips = info.mips,
//                            .layers = info.layers };
//     const auto found_handle = image_views.find(view);
//     if(!found_handle) { backend->make_view(view); }
//     auto it = image_views.insert(view);
//     if(!found_handle) { image_views_cache[info.image].push_back(it.handle); }
//     return it.handle;
// }

Handle<Sampler> Renderer::make_sampler(const SamplerDescriptor& info)
{
    return samplers.insert(backend->make_sampler(info));
}

Handle<Shader> Renderer::make_shader(const std::filesystem::path& path)
{
    const auto ext = std::filesystem::path{ path }.replace_extension().extension();
    ShaderStage stage;
    if(ext == ".vert") { stage = ShaderStage::VERTEX_BIT; }
    else if(ext == ".frag") { stage = ShaderStage::PIXEL_BIT; }
    else if(ext == ".comp") { stage = ShaderStage::COMPUTE_BIT; }
    else
    {
        ENG_ERROR("Unrecognized shader extension: {}", ext.string());
        return {};
    }
    Shader shader{ eng::paths::canonize_path(eng::paths::SHADERS_DIR / path), stage };
    const auto found_handle = shaders.find(shader);
    if(!found_handle) { backend->make_shader(shader); }
    auto it = shaders.insert(std::move(shader));
    if(!found_handle) { new_shaders.push_back(it.handle); }
    return it.handle;
}

Handle<DescriptorLayout> Renderer::make_layout(const DescriptorLayout& info)
{
    DescriptorLayout layout = info;
    const auto found_handle = dlayouts.find(layout);
    if(!found_handle) { backend->compile_layout(layout); }
    auto it = dlayouts.insert(std::move(layout));
    return it.handle;
}

Handle<PipelineLayout> Renderer::make_layout(const PipelineLayout& info)
{
    PipelineLayout layout = info;
    const auto found_handle = pplayouts.find(layout);
    if(!found_handle) { backend->compile_layout(layout); }
    auto it = pplayouts.insert(std::move(layout));
    return it.handle;
}

Handle<Pipeline> Renderer::make_pipeline(const PipelineCreateInfo& info)
{
    Pipeline p{ .info = info };
    if(!p.info.layout) { p.info.layout = common_playout; }
    const auto found_handle = pipelines.find(p);
    if(!found_handle) { backend->make_pipeline(p); }
    auto it = pipelines.insert(std::move(p));
    if(!found_handle) { new_pipelines.push_back(it.handle); }
    return it.handle;
}

Sync* Renderer::make_sync(const SyncCreateInfo& info) { return backend->make_sync(info); }

void Renderer::destroy_sync(Sync* sync) { backend->destory_sync(sync); }

Handle<Material> Renderer::make_material(const MaterialDescriptor& desc)
{
    auto meshpass = mesh_passes.find(MeshPass{ desc.mesh_pass });
    if(!meshpass) { meshpass = default_meshpass; }
    auto ret = materials.insert(Material{ .mesh_pass = meshpass, .base_color_texture = desc.base_color_texture });
    if(ret.success) { new_materials.push_back(ret.handle); }
    return ret.handle;
}

Handle<Geometry> Renderer::make_geometry(const GeometryDescriptor& batch)
{
    ENG_ASSERT((batch.vertex_layout & ~(VertexComponent::POSITION_BIT | VertexComponent::NORMAL_BIT |
                                        VertexComponent::TANGENT_BIT | VertexComponent::UV0_BIT)) == 0);

    std::vector<float> out_vertices;
    std::vector<uint16_t> out_indices;
    std::vector<Meshlet> out_meshlets;
    meshletize_geometry(batch, out_vertices, out_indices, out_meshlets);

    Geometry geometry{ .meshlet_range = { (uint32_t)meshlets.size(), (uint32_t)out_meshlets.size() } };

    const auto vertex_size = get_vertex_layout_size(batch.vertex_layout);
    const auto index_count = out_indices.size();
    const auto vertex_count = get_vertex_count(out_vertices, batch.vertex_layout);
    const auto pos_size = get_vertex_layout_size(VertexComponent::POSITION_BIT);
    const auto attr_size = vertex_size - pos_size;

    std::byte* psrcvx = (std::byte*)out_vertices.data();
    std::vector<std::byte> positions(vertex_count * pos_size);
    std::vector<std::byte> attributes(vertex_count * attr_size);
    for(auto i = 0ull; i < vertex_count; ++i)
    {
        memcpy(&positions[i * pos_size], &psrcvx[i * vertex_size], pos_size);
        memcpy(&attributes[i * attr_size], &psrcvx[i * vertex_size + pos_size], attr_size);
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
    sbuf->copy(bufs.positions, positions, STAGING_APPEND);
    sbuf->copy(bufs.attributes, attributes, STAGING_APPEND);
    sbuf->copy(bufs.indices, out_indices, STAGING_APPEND);
    sbuf->copy(bufs.bspheres, bounding_spheres, STAGING_APPEND);

    bufs.vertex_count += vertex_count;
    bufs.index_count += index_count;
    meshlets.insert(meshlets.end(), out_meshlets.begin(), out_meshlets.end());

    const auto handle = geometries.insert(std::move(geometry));

    ENG_LOG("Batching geometry: [VXS: {:.2f} KB, IXS: {:.2f} KB]",
            static_cast<float>(out_vertices.size() * sizeof(out_vertices[0])) / 1024.0f,
            static_cast<float>(out_indices.size() * sizeof(out_indices[0])) / 1024.0f);

    return handle;
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
    std::vector<meshopt_Bounds> mlt_bnds;
    std::vector<uint32_t> mlt_vxs(max_meshlets * max_verts);
    std::vector<uint8_t> mlt_ids(max_meshlets * max_tris * 3);

    const auto vx_size = get_vertex_layout_size(batch.vertex_layout);
    const auto vx_count = get_vertex_count(batch.vertices, batch.vertex_layout);
    const auto pos_size = get_vertex_component_size(VertexComponent::POSITION_BIT);
    const auto mltcnt = meshopt_buildMeshlets(mlts.data(), mlt_vxs.data(), mlt_ids.data(), indices.data(), indices.size(),
                                              batch.vertices.data(), vx_count, vx_size, max_verts, max_tris, cone_weight);
    const auto& last_mlt = mlts.at(mltcnt - 1);
    mlt_vxs.resize(last_mlt.vertex_offset + last_mlt.vertex_count);
    mlt_ids.resize(last_mlt.triangle_offset + ((last_mlt.triangle_count * 3 + 3) & ~3));
    mlts.resize(mltcnt);
    mlt_bnds.reserve(mltcnt);
    for(auto& m : mlts)
    {
        meshopt_optimizeMeshlet(&mlt_vxs.at(m.vertex_offset), &mlt_ids.at(m.triangle_offset), m.triangle_count, m.vertex_count);
        const auto mbounds = meshopt_computeMeshletBounds(&mlt_vxs.at(m.vertex_offset), &mlt_ids.at(m.triangle_offset),
                                                          m.triangle_count, batch.vertices.data(), vx_count, vx_size);
        mlt_bnds.push_back(mbounds);
    }
    out_vertices.resize(mlt_vxs.size() * vx_size / sizeof(float));
    for(auto i = 0ull; i < mlt_vxs.size(); ++i)
    {
        auto* pdst = (std::byte*)out_vertices.data();
        const auto* psrc = (const std::byte*)batch.vertices.data();
        memcpy(pdst + i * vx_size, psrc + mlt_vxs[i] * vx_size, vx_size);
    }

    out_indices.resize(mlt_ids.size());
    std::transform(mlt_ids.begin(), mlt_ids.end(), out_indices.begin(),
                   [](auto idx) { return static_cast<uint16_t>(idx); });
    out_meshlets.resize(mltcnt);
    for(auto i = 0u; i < mltcnt; ++i)
    {
        const auto& mlt = mlts.at(i);
        const auto& mltb = mlt_bnds.at(i);
        out_meshlets.at(i) =
            Meshlet{ .vertex_offset = (int32_t)mlt.vertex_offset,
                     .vertex_count = mlt.vertex_count,
                     .index_offset = mlt.triangle_offset,
                     .index_count = mlt.triangle_count * 3,
                     .bounding_sphere = glm::vec4{ mltb.center[0], mltb.center[1], mltb.center[2], mltb.radius } };
    }
}

Handle<Mesh> Renderer::make_mesh(const MeshDescriptor& batch)
{
    auto& bm = meshes.emplace_back(Mesh{ .geometry = batch.geometry, .material = batch.material });
    if(!bm.material) { bm.material = default_material; }
    return Handle<Mesh>{ (uint32_t)meshes.size() - 1 };
}

Handle<ShaderEffect> Renderer::make_shader_effect(const ShaderEffect& info)
{
    return shader_effects.insert(info).handle;
}

Handle<MeshPass> Renderer::make_mesh_pass(const MeshPass& info) { return mesh_passes.insert(info).handle; }

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

    auto dsth = make_buffer(Buffer::init(handle->name, new_size, handle->usage));
    auto& dst = dsth.get();
    if(copy_data)
    {
        if(new_size < handle->size) { ENG_WARN("Source data truncated as destination buffer is too small."); }
        sbuf->copy(dsth, handle, 0ull, { 0ull, std::min(new_size, handle->size) }, true);
    }
    destroy_buffer(handle);
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

void Renderer::update_transform(ecs::entity entity)
{
    if(Engine::get().ecs->get<ecs::Mesh>(entity)) { new_transforms.push_back(entity); }
    if(Engine::get().ecs->get<ecs::Light>(entity)) { new_lights.push_back(entity); }
}

SubmitQueue* Renderer::get_queue(QueueType type) { return backend->get_queue(type); }

uint32_t Renderer::get_framedata_index(int32_t offset) const { return (uint32_t)(frame_index % frames_in_flight); }

Renderer::FrameData& Renderer::get_framedata(int32_t offset) { return perframe.at(get_framedata_index(offset)); }

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

void Renderer::IndirectBatch::draw(const Callback<void(const IndirectDrawParams&)>& draw_callback) const
{
    size_t cmdoffacc = 0;
    for(auto i = 0u; i < batches.size(); ++i)
    {
        const auto& batch = batches[i];
        const auto cntoff = sizeof(uint32_t) * i;
        const auto cmdoff = get_renderer().backend->get_indirect_indexed_command_size() * cmdoffacc + cmds_view.range.offset;
        draw_callback(IndirectDrawParams{
            .batch = this,
            .draw = &batch,
            .max_draw_count = batch.command_count,
        });
        cmdoffacc += batch.command_count;
    }
}

void Renderer::DebugGeomBuffers::render(CommandBufferVk* cmd, Sync* s)
{
    ENG_ASSERT(s == nullptr);
    if(geometry.empty()) { return; }
    const auto verts = expand_into_vertices();
    ENG_ASSERT(verts.size() > geometry.size() && verts.size() % 2 == 0);
    if(!vpos_buf)
    {
        vpos_buf = Engine::get().renderer->make_buffer(Buffer::init("debug verts", verts.size() * sizeof(verts[0]),
                                                                    BufferUsage::STORAGE_BIT));
    }

    ENG_ASSERT(false);
    // Engine::get().renderer->sbuf->copy(vpos_buf, verts, 0);
    // Engine::get().renderer->sbuf->flush()->wait_cpu(~0ull);

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
