#pragma once

#include <meshoptimizer/src/meshoptimizer.h>
#include "renderer.hpp"
#include <eng/renderer/staging_buffer.hpp>
#include <eng/engine.hpp>
#include <eng/utils.hpp>
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
#include <assets/shaders/bindless_structures.glsli>

namespace eng
{

namespace gfx
{

struct RenderGraphPasses
{
    RenderGraph::ResourceView zbufsview;
    RenderGraph::ResourceView cbufsview;
    RenderGraph::ResourceView swapcbufsview;

    std::unique_ptr<pass::culling::ZPrepass> cull_zprepass;
    std::unique_ptr<pass::culling::Hiz> cull_hiz;
    std::unique_ptr<pass::culling::MainPass> cull_main;
    std::unique_ptr<pass::fwdp::LightCulling> fwdp_lightcull;
    std::unique_ptr<pass::DefaultUnlit> default_unlit;
    std::unique_ptr<pass::ImGui> imgui;
    std::unique_ptr<pass::PresentCopy> present_copy;
};

ImageBlockData GetBlockData(ImageFormat format)
{
    switch(format)
    {
    case ImageFormat::R8G8B8A8_UNORM:
    case ImageFormat::R8G8B8A8_SRGB:
    {
        return { 4, 1, 1, 1 };
    }
    default:
    {
        assert(false && "Bad format.");
        return {};
    }
    }
}

void Renderer::init(RendererBackend* backend)
{
    // clang-format off
    ENG_SET_HANDLE_GETTERS(Buffer,           { return &::eng::Engine::get().renderer->buffers.at(handle); });
    ENG_SET_HANDLE_GETTERS(Image,            { return &::eng::Engine::get().renderer->images.at(handle); });
    ENG_SET_HANDLE_GETTERS(ImageView,        { return &::eng::Engine::get().renderer->image_views.at(handle); });
    ENG_SET_HANDLE_GETTERS(Geometry,         { return &::eng::Engine::get().renderer->geometries.at(handle); });
    ENG_SET_HANDLE_GETTERS(Mesh,             { return &::eng::Engine::get().renderer->meshes.at(*handle); });
    ENG_SET_HANDLE_GETTERS(Texture,          { return &::eng::Engine::get().renderer->textures.at(handle); });
    ENG_SET_HANDLE_GETTERS(Material,         { return &::eng::Engine::get().renderer->materials.at(handle); });
    ENG_SET_HANDLE_GETTERS(Shader,           { return &::eng::Engine::get().renderer->shaders.at(handle); });
    ENG_SET_HANDLE_GETTERS(PipelineLayout,   { return &::eng::Engine::get().renderer->pplayouts.at(handle); });
    ENG_SET_HANDLE_GETTERS(Pipeline,         { return &::eng::Engine::get().renderer->pipelines.at(handle); });
    ENG_SET_HANDLE_GETTERS(Sampler,          { return &::eng::Engine::get().renderer->samplers.at(handle); });
    ENG_SET_HANDLE_GETTERS(MeshPass,         { return &::eng::Engine::get().renderer->mesh_passes.at(handle); });
    ENG_SET_HANDLE_GETTERS(ShaderEffect,     { return &::eng::Engine::get().renderer->shader_effects.at(handle); });
    ENG_SET_HANDLE_GETTERS(DescriptorPool,   { return &::eng::Engine::get().renderer->descpools.at(*handle); });
    // clang-format on

    this->backend = backend;
    backend->init();

    gq = backend->get_queue(QueueType::GRAPHICS);
    swapchain = backend->make_swapchain();
    sbuf = new StagingBuffer{};
    sbuf->init(gq, [this](auto buf) { bindless->update_index(buf); });
    rgraph = new RenderGraph{};
    rgraph->init(this);

    init_bufs();
    init_perframes();
    init_pipelines();
    init_helper_geom();
    init_rgraph_passes();

    imgui_renderer = new ImGuiRenderer{};
    imgui_renderer->init();

    ecs_mesh_view = Engine::get().ecs->get_view<ecs::Transform, ecs::Mesh>(
        [this](ecs::entity e) {
            new_transforms.push_back(e);
            const auto* mesh = Engine::get().ecs->get<ecs::Mesh>(e);
            for(const auto& rmesh : mesh->meshes)
            {
                const auto& eff = rmesh->material->mesh_pass->effects;
                for(auto i = 0u; i < eff.size(); ++i)
                {
                    const auto rpt = (RenderPassType)i;
                    if(eff.at(i))
                    {
                        render_passes.at(rpt).entities.push_back(e);
                        render_passes.at(rpt).redo = true;
                    }
                }
            }
        },
        [this](ecs::entity e, ecs::signature_t sig) {
            if(sig.test(ecs::get_id<ecs::Transform>())) { new_transforms.push_back(e); }
            if(sig.test(ecs::get_id<ecs::Mesh>()))
            {
                ENG_TODO();
                assert(false);
            }
        });
    ecs_light_view = Engine::get().ecs->get_view<ecs::Light>([this](auto e) { new_lights.push_back(e); },
                                                             [this](auto e, auto sig) { new_lights.push_back(e); });

    Engine::get().ui->add_tab(UI::Tab{
        "Debug",
        UI::Location::RIGHT_PANE,
        [this] {
            if(!Engine::get().ui->show_debug_tab) { return; }
            if(ImGui::Begin("Debug"))
            {
                auto* camera = Engine::get().camera;
                ImGui::SeparatorText("Camera");
                ImGui::Text("Position: %.2f %.2f %.2f", camera->pos.x, camera->pos.y, camera->pos.z);

                ImGui::SeparatorText("Forward+");
                ImGui::Text("Tile size: %u px", bufs.fwdp_tile_pixels);
                ImGui::Text("Num tiles: %u", bufs.fwdp_num_tiles);
                ImGui::Text("Lights per tile: %u", bufs.fwdp_lights_per_tile);

                bool fwdp_grid_output = debug_output == DebugOutput::FWDP_GRID;
                bool changed = false;
                changed |= ImGui::Checkbox("FWDP heatmap", &fwdp_grid_output);
                if(fwdp_grid_output) { debug_output = DebugOutput::FWDP_GRID; }
                else { debug_output = DebugOutput::COLOR; }
                ImGui::Checkbox("FWDP enable", &fwdp_enable);

                ImGui::SeparatorText("Culling");
                const auto& ppf = get_perframe(-1);
                const auto& cullbuf = rgraph->get_resource(rgraphpasses->cull_zprepass->culled_id_bufs.get(-1)).buffer.get();
                uint32_t mlts = 0;
                for(auto [e, t, m] : ecs_mesh_view)
                {
                    const auto& msh = Engine::get().ecs->get<ecs::Mesh>(e);
                    for(auto mmsh : msh->meshes)
                    {
                        mlts += mmsh->geometry->meshlet_range.size;
                    }
                }
                ImGui::Text("Drawn meshlets: %u", *(uint32_t*)cullbuf.memory);
                float culled_percent = (float)(*(uint32_t*)cullbuf.memory) / mlts;
                ImGui::Text("%.2f%% of meshlets culled", 100.0f - culled_percent * 100.0f);
                ImGui::Checkbox("Meshlet frustum culling", &mlt_frust_cull_enable);
                ImGui::Checkbox("Meshlet occlusion culling", &mlt_occ_cull_enable);
            }
            ImGui::End();
        },
    });
}

void Renderer::init_helper_geom()
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const auto gen_uv_sphere = [&vertices, &indices] {
        const auto segs = 16;
        const auto rings = 16;
        vertices.clear();
        indices.clear();
        vertices.reserve(segs * rings);
        indices.reserve((rings - 1) * (segs - 1) * 6);
        for(auto y = 0u; y < rings; ++y)
        {
            const auto v = (float)y / (float)(rings - 1);
            const auto theta = v * glm::pi<float>();
            const auto st = std::sinf(theta);
            const auto ct = std::cosf(theta);
            for(auto x = 0u; x < segs; ++x)
            {
                const auto u = (float)x / (float)(segs - 1);
                const auto phi = u * 2.0f * glm::pi<float>();
                const auto sp = std::sinf(phi);
                const auto cp = std::cosf(phi);
                vertices.push_back(Vertex{ .position = { st * cp, ct, st * sp }, .uv = { u, v } });
            }
        }
        for(auto y = 0u; y < rings - 1; ++y)
        {
            for(auto x = 0u; x < segs - 1; ++x)
            {
                const auto idx = y * segs + x;
                indices.push_back(idx);
                indices.push_back(idx + 1);
                indices.push_back(idx + segs);
                indices.push_back(idx + segs);
                indices.push_back(idx + 1);
                indices.push_back(idx + segs + 1);
            }
        }
    };

    gen_uv_sphere();
    assert(vertices.size() <= ~uint16_t{});
    helpergeom.uvsphere = make_geometry(GeometryDescriptor{ .vertices = vertices, .indices = indices });
    helpergeom.ppskybox = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("common/skybox.vert.glsl"),
                     Engine::get().renderer->make_shader("common/skybox.frag.glsl") },
        .layout = bindless_pplayout,
        .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
        .depth_test = true,
        .depth_write = true,
        .depth_compare = DepthCompare::GREATER,
        .culling = CullFace::BACK,
    });
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

    const auto bindless_bflags = PipelineBindingFlags::UPDATE_AFTER_BIND_BIT |
                                 PipelineBindingFlags::UPDATE_UNUSED_WHILE_PENDING_BIT | PipelineBindingFlags::PARTIALLY_BOUND_BIT;
    bindless_pplayout = make_pplayout(PipelineLayoutCreateInfo{
        .sets = { PipelineLayoutCreateInfo::SetLayout{
            .flags = PipelineSetFlags::UPDATE_AFTER_BIND_BIT,
            .bindings = {
                { PipelineBindingType::STORAGE_BUFFER, BINDLESS_STORAGE_BUFFER_BINDING, 1024, ShaderStage::ALL, bindless_bflags },
                { PipelineBindingType::STORAGE_IMAGE, BINDLESS_STORAGE_IMAGE_BINDING, 1024, ShaderStage::ALL, bindless_bflags },
                { PipelineBindingType::SAMPLED_IMAGE, BINDLESS_SAMPLED_IMAGE_BINDING, 1024, ShaderStage::ALL, bindless_bflags },
                { PipelineBindingType::SEPARATE_SAMPLER, BINDLESS_SAMPLER_BINDING, std::size(imsamplers), ShaderStage::ALL, bindless_bflags, imsamplers },
            } } },
        .range = {ShaderStage::ALL, 128},
        });
    bindless_pool = make_descpool(DescriptorPoolCreateInfo{
        .flags = DescriptorPoolFlags::UPDATE_AFTER_BIND_BIT,
        .max_sets = 2,
        .pools = { { PipelineBindingType::STORAGE_BUFFER, 2 * 1024 },
                   { PipelineBindingType::STORAGE_IMAGE, 2 * 1024 },
                   { PipelineBindingType::SAMPLED_IMAGE, 2 * 1024 },
                   { PipelineBindingType::SEPARATE_SAMPLER, 2 * 2 } },
    });
    bindless_set = bindless_pool->allocate(bindless_pplayout, 0);
    bindless = new BindlessPool{ bindless_pool, bindless_set };

    hiz_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("culling/hiz.comp.glsl") }, .layout = bindless_pplayout });
    cull_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("culling/culling.comp.glsl") },
        .layout = bindless_pplayout,
    });
    cullzout_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("common/zoutput.vert.glsl"),
                     Engine::get().renderer->make_shader("common/zoutput.frag.glsl") },
        .layout = bindless_pplayout,
        .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
        .depth_test = true,
        .depth_write = true,
        .depth_compare = DepthCompare::GREATER,
        .culling = CullFace::BACK,
    });
    fwdp_cull_lights_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("forwardp/cull_lights.comp.glsl") },
        .layout = bindless_pplayout,
    });
    default_unlit_pipeline = make_pipeline(PipelineCreateInfo{
        .shaders = { make_shader("default_unlit/unlit.vert.glsl"), make_shader("default_unlit/unlit.frag.glsl") },
        .layout = bindless_pplayout,
        .attachments = { .count = 1,
                         .color_formats = { ImageFormat::R8G8B8A8_SRGB },
                         .blend_states = { PipelineCreateInfo::BlendState{ .enable = true,
                                                                           .src_color_factor = BlendFactor::SRC_ALPHA,
                                                                           .dst_color_factor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                                                                           .color_op = BlendOp::ADD,
                                                                           .src_alpha_factor = BlendFactor::ONE,
                                                                           .dst_alpha_factor = BlendFactor::ZERO,
                                                                           .alpha_op = BlendOp::ADD } },
                         .depth_format = ImageFormat::D32_SFLOAT },
        .depth_test = true,
        .depth_write = false,
        .depth_compare = DepthCompare::GEQUAL,
        .culling = CullFace::BACK,
    });
    MeshPassCreateInfo info{ .name = "default_unlit" };
    info.effects[(uint32_t)RenderPassType::FORWARD] = make_shader_effect(ShaderEffect{ .pipeline = default_unlit_pipeline });
    default_meshpass = make_mesh_pass(info);
    default_material = materials.insert(Material{ .mesh_pass = default_meshpass }).handle;
}

void Renderer::init_perframes()
{
    auto* ew = Engine::get().window;
    perframe.resize(frame_count);
    for(auto i = 0u; i < frame_count; ++i)
    {
        auto& pf = perframe[i];
        auto& gb = pf.gbuffer;
        gb.color = make_image(ImageDescriptor{
            .name = ENG_FMT("gcolor{}", i),
            .width = (uint32_t)ew->width,
            .height = (uint32_t)ew->height,
            .format = ImageFormat::R8G8B8A8_SRGB,
            .usage = ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::TRANSFER_RW | ImageUsage::SAMPLED_BIT,
        });
        gb.depth = make_image(ImageDescriptor{
            .name = ENG_FMT("gdepth{}", i),
            .width = (uint32_t)ew->width,
            .height = (uint32_t)ew->height,
            .format = ImageFormat::D32_SFLOAT,
            .usage = ImageUsage::DEPTH_BIT | ImageUsage::TRANSFER_RW | ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT,
        });
        pf.cmdpool = gq->make_command_pool();
        pf.acq_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("acquire semaphore {}", i) });
        pf.ren_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("rendering semaphore {}", i) });
        pf.ren_fen = make_sync({ SyncType::FENCE, 1, ENG_FMT("rendering fence {}", i) });
        pf.swp_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 1, ENG_FMT("swap semaphore {}", i) });
        pf.constants =
            make_buffer(BufferDescriptor{ ENG_FMT("constants_{}", i), sizeof(GPUEngConstantsBuffer), BufferUsage::STORAGE_BIT });
    }
}

void Renderer::init_bufs()
{
    bufs.vpos_buf = make_buffer(BufferDescriptor{ "vertex positions", 1024, BufferUsage::STORAGE_BIT });
    bufs.vattr_buf = make_buffer(BufferDescriptor{ "vertex attributes", 1024, BufferUsage::STORAGE_BIT });
    bufs.idx_buf = make_buffer(BufferDescriptor{ "vertex indices", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDEX_BIT });
    bufs.bsphere_buf = make_buffer(BufferDescriptor{ "bounding spheres", 1024, BufferUsage::STORAGE_BIT });
    bufs.mats_buf = make_buffer(BufferDescriptor{ "materials", 1024, BufferUsage::STORAGE_BIT });
    for(uint32_t i = 0; i < 2; ++i)
    {
        bufs.trs_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("trs {}", i), 1024, BufferUsage::STORAGE_BIT });
        bufs.lights_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("lights {}", i), 256, BufferUsage::STORAGE_BIT });
    }
    for(auto i = 0u; i < (uint32_t)RenderPassType::LAST_ENUM; ++i)
    {
        const auto rpt = (RenderPassType)i;
        render_passes[rpt].batch.cmd_buf = make_buffer(BufferDescriptor{
            ENG_FMT("{}_cmds", to_string(rpt)), 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
        render_passes[rpt].batch.ids_buf =
            make_buffer(BufferDescriptor{ ENG_FMT("{}_ids", to_string(rpt)), 1024, BufferUsage::STORAGE_BIT });
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
    rgraphpasses = new RenderGraphPasses{};
    std::vector<Handle<Image>> zbufs(frame_count);
    std::vector<Handle<Image>> cbufs(frame_count);
    std::vector<Handle<Image>> swapcbufs(frame_count);
    for(auto i = 0u; i < frame_count; ++i)
    {
        zbufs[i] = perframe[i].gbuffer.depth;
        cbufs[i] = perframe[i].gbuffer.color;
        swapcbufs[i] = swapchain->images[i];
    }
    rgraphpasses->zbufsview = rgraph->import_resource(std::span{ zbufs });
    rgraphpasses->cbufsview = rgraph->import_resource(std::span{ cbufs });
    rgraphpasses->swapcbufsview = rgraph->import_resource(std::span{ swapcbufs });

    rgraphpasses->cull_zprepass =
        std::make_unique<pass::culling::ZPrepass>(rgraph, pass::culling::ZPrepass::CreateInfo{ rgraphpasses->zbufsview });
    rgraphpasses->cull_hiz =
        std::make_unique<pass::culling::Hiz>(rgraph, pass::culling::Hiz::CreateInfo{ rgraphpasses->zbufsview });
    rgraphpasses->cull_main =
        std::make_unique<pass::culling::MainPass>(rgraph, pass::culling::MainPass::CreateInfo{
                                                              &render_passes.at(RenderPassType::FORWARD),
                                                              &*rgraphpasses->cull_zprepass, &*rgraphpasses->cull_hiz });

    rgraphpasses->cull_zprepass->ibatches = &rgraphpasses->cull_main->batches; // todo: don't like this

    rgraphpasses->fwdp_lightcull =
        std::make_unique<pass::fwdp::LightCulling>(rgraph, pass::fwdp::LightCulling::CreateInfo{
                                                               rgraphpasses->zbufsview, bufs.fwdp_num_tiles,
                                                               bufs.fwdp_lights_per_tile, bufs.fwdp_tile_pixels });
    rgraphpasses->default_unlit =
        std::make_unique<pass::DefaultUnlit>(rgraph, pass::DefaultUnlit::CreateInfo{
                                                         &*rgraphpasses->cull_zprepass, &*rgraphpasses->fwdp_lightcull,
                                                         rgraphpasses->cbufsview, rgraphpasses->zbufsview });
    rgraphpasses->imgui = std::make_unique<pass::ImGui>(rgraph, pass::ImGui::CreateInfo{ rgraphpasses->cbufsview });
    rgraphpasses->present_copy =
        std::make_unique<pass::PresentCopy>(rgraph, pass::PresentCopy::CreateInfo{ rgraphpasses->cbufsview,
                                                                                   rgraphpasses->swapcbufsview, swapchain });
}

void Renderer::update()
{
    auto* ew = Engine::get().window;
    const auto pfi = Engine::get().frame_num % Renderer::frame_count;
    auto& pf = get_perframe();
    const auto& ppf = perframe.at((Engine::get().frame_num + perframe.size() - 1) % perframe.size()); // get previous frame res

    pf.ren_fen->wait_cpu(~0ull);
    pf.ren_fen->reset();
    pf.acq_sem->reset();
    pf.ren_sem->reset();
    pf.swp_sem->reset();
    pf.cmdpool->reset();
    sbuf->reset();
    swapchain->acquire(~0ull, pf.acq_sem);

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
            GPUMaterial gpumat{ .base_color_idx = get_bindless(e->base_color_texture) };
            sbuf->copy(bufs.mats_buf, &gpumat, *e * sizeof(gpumat), sizeof(gpumat));
        }
        new_materials.clear();
    }
    if(new_transforms.size())
    {
        std::swap(bufs.trs_bufs[0], bufs.trs_bufs[1]);
        sbuf->copy(bufs.trs_bufs[0], bufs.trs_bufs[1], 0, { 0, bufs.trs_bufs[1]->size });
        for(auto i = 0u; i < new_transforms.size(); ++i)
        {
            const auto* trs = Engine::get().ecs->get<ecs::Transform>(new_transforms.at(i));
            const auto* msh = Engine::get().ecs->get<ecs::Mesh>(new_transforms.at(i));
            sbuf->copy(bufs.trs_bufs[0], &trs->global, msh->gpu_resource * sizeof(trs->global), sizeof(trs->global));
        }
        new_transforms.clear();
    }
    if(new_lights.size())
    {
        std::swap(bufs.lights_bufs[0], bufs.lights_bufs[1]);
        sbuf->copy(bufs.lights_bufs[0], bufs.lights_bufs[1], 0, { 0, bufs.lights_bufs[1]->size });
        for(auto i = 0u; i < new_lights.size(); ++i)
        {
            auto* l = Engine::get().ecs->get<ecs::Light>(new_lights.at(i));
            const auto* t = Engine::get().ecs->get<ecs::Transform>(new_lights.at(i));
            if(l->gpu_index == ~0u) { l->gpu_index = gpu_light_allocator.allocate_slot(); }
            GPULight gpul{ t->pos(), l->range, l->color, l->intensity, (uint32_t)l->type };
            sbuf->copy(bufs.lights_bufs[0], &gpul,
                       offsetof(GPULightsBuffer, lights_us) + l->gpu_index * sizeof(GPULight), sizeof(GPULight));
        }
        const auto lc = (uint32_t)ecs_light_view.size();
        sbuf->copy(bufs.lights_bufs[0], &lc, 0, 4);
        new_lights.clear();
    }

    const auto view = Engine::get().camera->get_view();
    const auto proj = Engine::get().camera->get_projection();
    const auto invview = glm::inverse(view);
    const auto invproj = glm::inverse(proj);

    static auto prev_view = view;
    if(true || glfwGetKey(Engine::get().window->window, GLFW_KEY_EQUAL) == GLFW_PRESS) { prev_view = view; }

    GPUEngConstantsBuffer cb{
        .vposb = get_bindless(bufs.vpos_buf),
        .vatrb = get_bindless(bufs.vattr_buf),
        .vidxb = get_bindless(bufs.idx_buf),
        .GPUBoundingSpheresBufferIndex = get_bindless(bufs.bsphere_buf),
        .GPUTransformsBufferIndex = get_bindless(bufs.trs_bufs[0]),
        .rmatb = get_bindless(bufs.mats_buf),
        .GPULightsBufferIndex = get_bindless(bufs.lights_bufs[0]),
        .view = view,
        .prev_view = prev_view,
        .proj = proj,
        .proj_view = proj * view,
        .inv_view = invview,
        .inv_proj = invproj,
        .inv_proj_view = invview * invproj,
        //.rand_mat = rand_mat,
        .cam_pos = Engine::get().camera->pos,

        .output_mode = (uint32_t)debug_output,
        .fwdp_enable = (uint32_t)fwdp_enable,
        .fwdp_max_lights_per_tile = bufs.fwdp_lights_per_tile,
        .mlt_frust_cull_enable = (uint32_t)mlt_frust_cull_enable,
        .mlt_occ_cull_enable = (uint32_t)mlt_occ_cull_enable,
    };
    sbuf->copy(pf.constants, &cb, 0ull, sizeof(cb));
    // sbuf->flush()->wait_cpu(~0ull);

    //{
    //    const uint32_t zero = 0u;
    //    sbuf->copy(pf.fwdp.light_list_buf, &zero, 0ull, 4);
    //}
    // sbuf->flush()->wait_cpu(~0ull);

    rgraph->add_pass(&*rgraphpasses->cull_zprepass);
    rgraph->add_pass(&*rgraphpasses->cull_hiz);
    rgraph->add_pass(&*rgraphpasses->cull_main);
    rgraph->add_pass(&*rgraphpasses->fwdp_lightcull);
    rgraph->add_pass(&*rgraphpasses->default_unlit);
    rgraph->add_pass(&*rgraphpasses->imgui);
    rgraph->add_pass(&*rgraphpasses->present_copy);
    rgraph->compile();
    auto* rgsync = rgraph->execute(sbuf->flush());

    auto* cmd = pf.cmdpool->begin();
    cmd->barrier(swapchain->get_image().get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::ALL,
                 PipelineAccess::NONE, ImageLayout::TRANSFER_DST, ImageLayout::PRESENT);
    pf.cmdpool->end(cmd);

    gq->wait_sync(pf.acq_sem, PipelineStage::ALL)
        .wait_sync(rgsync, PipelineStage::ALL)
        .with_cmd_buf(cmd)
        .signal_sync(pf.swp_sem, PipelineStage::NONE)
        .signal_sync(pf.ren_fen)
        .submit();
    gq->wait_sync(pf.swp_sem, PipelineStage::NONE).present(swapchain);
    gq->wait_idle();
}

void Renderer::render(RenderPassType pass, SubmitQueue* queue, CommandBuffer* cmd)
{
    auto* ew = Engine::get().window;
    auto& pf = get_perframe();
    auto& rp = render_passes.at(pass);

    const VkRenderingAttachmentInfo vkcols[] = { Vks(VkRenderingAttachmentInfo{
        .imageView = pf.gbuffer.color->default_view->md.vk->view,
        .imageLayout = to_vk(ImageLayout::ATTACHMENT),
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .uint32 = {} } } }) };
    const auto vkdep = Vks(VkRenderingAttachmentInfo{ .imageView = pf.gbuffer.depth->default_view->md.vk->view,
                                                      .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                                      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                                      .storeOp = VK_ATTACHMENT_STORE_OP_STORE });
    VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
    VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
    const auto vkreninfo = Vks(VkRenderingInfo{
        .renderArea = vksciss, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = vkcols, .pDepthAttachment = &vkdep });

    // todo: mbatches might be empty. is this bad that i run this?
    cmd->bind_index(bufs.idx_buf.get(), 0, bufs.index_type);
    cmd->set_scissors(&vksciss, 1);
    cmd->set_viewports(&vkview, 1);
    cmd->begin_rendering(vkreninfo);
    const auto& maincullpass = rgraph->get_pass<pass::culling::MainPass>("culling::MainPass");
    const auto& ib = maincullpass.batches[get_perframe_index()];
    render_ibatch(cmd, ib, [this, &ib, &pf](CommandBuffer* cmd) {
        const auto outputmode = (uint32_t)debug_output;
        cmd->bind_resource(0, pf.constants);
        cmd->bind_resource(1, ib.ids_buf);
        const auto& fwd = rgraph->get_pass<pass::fwdp::LightCulling>("fwdp::LightCulling");
        cmd->bind_resource(2, rgraph->get_resource(fwd.culled_light_grid_bufs).buffer);
        cmd->bind_resource(3, rgraph->get_resource(fwd.culled_light_list_bufs).buffer);
    });
    if(0)
    {
        const auto& geom = helpergeom.uvsphere.get();
        cmd->bind_pipeline(helpergeom.ppskybox.get());
        for(auto mli = 0u; mli < geom.meshlet_range.size; ++mli)
        {
            const auto& ml = meshlets.at(geom.meshlet_range.offset + mli);
            cmd->draw_indexed(ml.index_count, 1, ml.index_offset, ml.vertex_offset, 0);
        }
    }
    cmd->end_rendering();
}

void Renderer::build_renderpasses()
{
    struct MeshInstance
    {
        Handle<Geometry> geom;
        Handle<Material> material;
        uint32_t id;
        uint32_t mlti;
    };
    struct RPassData
    {
        std::vector<MeshInstance> minsts;
    };
    std::unordered_map<RenderPassType, RPassData> rpdatas;

    for(const auto& [rpt, rp] : render_passes)
    {
        if(!rp.redo) { continue; }

        for(const auto& e : rp.entities)
        {
            auto& m = *Engine::get().ecs->get<ecs::Mesh>(e);
            if(m.gpu_resource == ~0u) { m.gpu_resource = gpu_resource_allocator.allocate_slot(); }
            for(auto i = 0u; i < m.meshes.size(); ++i)
            {
                const auto& mesh = m.meshes.at(i).get();
                const auto& geom = mesh.geometry.get();
                const auto& meshpass = mesh.material->mesh_pass.get();
                for(auto j = 0u; j < geom.meshlet_range.size; ++j)
                {
                    rpdatas[rpt].minsts.push_back(MeshInstance{ mesh.geometry, mesh.material, m.gpu_resource,
                                                                geom.meshlet_range.offset + j });
                }
            }
        }
    }

    for(auto& [rpt, rpd] : rpdatas)
    {
        auto& rp = render_passes.at(rpt);
        rp.redo = false;

        // first sort by material, so pipelines can be the same (batch by pipelines)
        // then sort by meshlet so indirect command struct can be the same (batch by instances)
        std::sort(rpd.minsts.begin(), rpd.minsts.end(), [](const MeshInstance& a, const MeshInstance& b) {
            if(a.material >= b.material) { return false; }
            if(a.mlti >= b.mlti) { return false; }
            return true;
        });

        Handle<Pipeline> pp;
        uint32_t cmdi = ~0u;
        uint32_t mlti = ~0u;
        uint32_t batchi = ~0u;
        rp.batch.batches.clear();
        rp.batch.batches.resize(rpd.minsts.size());
        std::vector<DrawIndexedIndirectCommand> cmds(rpd.minsts.size());
        std::vector<uint32_t> cmdcnts(rpd.minsts.size());
        std::vector<GPUInstanceId> gpuids(rpd.minsts.size());
        for(auto i = 0u; i < rpd.minsts.size(); ++i)
        {
            const auto& minst = rpd.minsts.at(i);
            const auto& geom = minst.geom.get();
            const auto& mlt = meshlets.at(minst.mlti);
            const auto& mat = minst.material.get();
            const auto& mpp = mat.mesh_pass->effects.at((uint32_t)rpt)->pipeline;
            // pipeline changed
            if(mpp != pp)
            {
                pp = mpp;
                rp.batch.batches.at(++batchi).pipeline = pp;
            }
            // indirect command changed
            if(minst.mlti != mlti)
            {
                mlti = minst.mlti;
                cmds.at(++cmdi) = DrawIndexedIndirectCommand{ .indexCount = mlt.index_count,
                                                              .instanceCount = 0,
                                                              .firstIndex = mlt.index_offset,
                                                              .vertexOffset = mlt.vertex_offset,
                                                              .firstInstance = i };
            }
            gpuids.at(i) = GPUInstanceId{ cmdi, minst.mlti, minst.id, *minst.material };
            // don't increment the indirect command instcount here - this the culling stage handles
            ++rp.batch.batches.at(batchi).inst_count;
            rp.batch.batches.at(batchi).cmd_count = cmdi + 1 - (batchi > 0 ? rp.batch.batches.at(batchi - 1).cmd_count : 0);
            ++cmdcnts.at(batchi); // = rp.batch.batches.at(batchi).cmd_count;
        }

        ++cmdi;
        ++batchi;
        cmds.resize(cmdi);
        cmdcnts.resize(batchi);
        const auto cmdoff = align_up2((batchi + 1) * sizeof(uint32_t), 16ull);
        rp.batch.batches.resize(batchi);
        rp.batch.cmd_count = cmdi;
        rp.batch.cmd_start = cmdoff;
        rp.batch.ids_count = gpuids.size();
        rp.redo = false;

        sbuf->copy(rp.batch.cmd_buf, cmdcnts, 0);
        sbuf->copy(rp.batch.cmd_buf, cmds, cmdoff);
        sbuf->copy(rp.batch.ids_buf, &rp.batch.ids_count, 0, 4);
        sbuf->copy(rp.batch.ids_buf, gpuids, 4);
    }
}

void Renderer::render_ibatch(CommandBuffer* cmd, const IndirectBatch& ibatch,
                             const Callback<void(CommandBuffer*)>& setup_resources, bool bind_pps)
{
    auto cmdoffacc = 0u;
    for(auto i = 0u; i < ibatch.batches.size(); ++i)
    {
        const auto& mb = ibatch.batches.at(i);
        const auto cntoff = sizeof(uint32_t) * i;
        const auto cmdoff = sizeof(DrawIndexedIndirectCommand) * cmdoffacc + ibatch.cmd_start;
        if(bind_pps) { cmd->bind_pipeline(mb.pipeline.get()); }
        if(i == 0 && setup_resources) { setup_resources(cmd); }
        cmd->draw_indexed_indirect_count(ibatch.cmd_buf.get(), cmdoff, ibatch.cmd_buf.get(), cntoff, mb.cmd_count,
                                         sizeof(DrawIndexedIndirectCommand));
        cmdoffacc += mb.cmd_count;
    }
}

Handle<Buffer> Renderer::make_buffer(const BufferDescriptor& info)
{
    uint32_t order = 0;
    float size = (float)info.size;
    for(; size >= 1024.0f && order < 4; size /= 1024.0f, ++order) {}
    static constexpr const char* units[]{ "B", "KB", "MB", "GB" };
    ENG_LOG("Creating buffer {} [{:.2f} {}]", info.name, size, units[order]);
    return buffers.insert(backend->make_buffer(info));
}

Handle<Image> Renderer::make_image(const ImageDescriptor& info)
{
    auto h = images.insert(backend->make_image(info));
    h->default_view = make_view(ImageViewDescriptor{ .name = ENG_FMT("{}_default", info.name), .image = h });
    if(info.data.size_bytes())
    {
        sbuf->copy(h, info.data.data());
        for(auto i = 0u; i < info.mips - 1; ++i)
        {
            const Range3D32i srcsz{
                { 0, 0, 0 }, { std::max(info.width >> i, 1u), std::max(info.height >> i, 1u), std::max(info.depth >> i, 1u) }
            };
            const Range3D32i dstsz{ { 0, 0, 0 },
                                    { std::max(srcsz.size.x >> 1, 1), std::max(srcsz.size.y >> 1, 1),
                                      std::max(srcsz.size.z >> 1, 1) } };
            sbuf->barrier(h, ImageLayout::TRANSFER_DST, ImageLayout::TRANSFER_SRC, ImageSubRange{ { i, 1 }, { 0, 1 } });
            sbuf->blit(h, h, ImageBlit{ { i, { 0, 1 } }, { i + 1, { 0, 1 } }, srcsz, dstsz });
        }
        sbuf->barrier(h, ImageLayout::TRANSFER_SRC, ImageLayout::READ_ONLY, ImageSubRange{ { 0, info.mips - 1 }, { 0, 1 } });
        sbuf->barrier(h, ImageLayout::TRANSFER_DST, ImageLayout::READ_ONLY, ImageSubRange{ { info.mips - 1, 1 }, { 0, 1 } });
        h->current_layout = ImageLayout::READ_ONLY;
    }
    return h;
}

Handle<ImageView> Renderer::make_view(const ImageViewDescriptor& info)
{
    auto& img = Handle{ info.image }.get();
    auto view = ImageView{ .name = info.name,
                           .image = info.image,
                           .type = info.view_type ? *info.view_type : img.deduce_view_type(),
                           .format = info.format ? *info.format : img.format,
                           .aspect = info.aspect ? *info.aspect : img.deduce_aspect(),
                           .mips = info.mips,
                           .layers = info.layers };
    const auto found_handle = image_views.find(view);
    if(!found_handle) { backend->make_view(view); }
    auto it = image_views.insert(view);
    if(!found_handle) { image_views_cache[info.image].push_back(it.handle); }
    return it.handle;
}

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

Handle<PipelineLayout> Renderer::make_pplayout(const PipelineLayoutCreateInfo& info)
{
    PipelineLayout layout{ .info = info };
    const auto found_handle = pplayouts.find(layout);
    if(!found_handle) { backend->compile_pplayout(layout); }
    auto it = pplayouts.insert(std::move(layout));
    return it.handle;
}

Handle<Pipeline> Renderer::make_pipeline(const PipelineCreateInfo& info)
{
    Pipeline p{ .info = info };
    const auto found_handle = pipelines.find(p);
    if(!found_handle) { backend->make_pipeline(p); }
    auto it = pipelines.insert(std::move(p));
    if(!found_handle) { new_pipelines.push_back(it.handle); }
    return it.handle;
}

Sync* Renderer::make_sync(const SyncCreateInfo& info) { return backend->make_sync(info); }

Handle<Texture> Renderer::make_texture(const TextureDescriptor& batch)
{
    return textures.insert(Texture{ batch.view, batch.layout, batch.is_storage }).handle;
}

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
    std::vector<Vertex> out_vertices;
    std::vector<uint16_t> out_indices;
    std::vector<Meshlet> out_meshlets;
    meshletize_geometry(batch, out_vertices, out_indices, out_meshlets);

    Geometry geometry{ .meshlet_range = { (uint32_t)meshlets.size(), (uint32_t)out_meshlets.size() } };

    static constexpr auto VXATTRSIZE = sizeof(Vertex) - sizeof(Vertex::position);
    std::vector<glm::vec3> positions(out_vertices.size());
    std::vector<std::byte> attributes(out_vertices.size() * VXATTRSIZE);
    for(auto i = 0ull; i < out_vertices.size(); ++i)
    {
        auto& v = out_vertices.at(i);
        positions[i] = v.position;
        memcpy(&attributes[i * VXATTRSIZE], reinterpret_cast<const std::byte*>(&v) + sizeof(Vertex::position), VXATTRSIZE);
    }
    std::vector<glm::vec4> bounding_spheres(out_meshlets.size());
    for(auto i = 0u; i < out_meshlets.size(); ++i)
    {
        out_meshlets.at(i).vertex_offset += bufs.vertex_count;
        out_meshlets.at(i).index_offset += bufs.index_count;
        bounding_spheres.at(i) = out_meshlets.at(i).bounding_sphere;
    }

    sbuf->copy(bufs.vpos_buf, positions, STAGING_APPEND);
    sbuf->copy(bufs.vattr_buf, attributes, STAGING_APPEND);
    sbuf->copy(bufs.idx_buf, out_indices, STAGING_APPEND);
    sbuf->copy(bufs.bsphere_buf, bounding_spheres, STAGING_APPEND);

    bufs.vertex_count += positions.size();
    bufs.index_count += out_indices.size();
    meshlets.insert(meshlets.end(), out_meshlets.begin(), out_meshlets.end());

    const auto handle = geometries.insert(std::move(geometry));

    ENG_LOG("Batching geometry: [VXS: {:.2f} KB, IXS: {:.2f} KB]", static_cast<float>(batch.vertices.size_bytes()) / 1024.0f,
            static_cast<float>(batch.indices.size_bytes()) / 1024.0f);

    return handle;
}

void Renderer::meshletize_geometry(const GeometryDescriptor& batch, std::vector<Vertex>& out_vertices,
                                   std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets)
{
    static constexpr auto max_verts = 64u;
    static constexpr auto max_tris = 124u;
    static constexpr auto cone_weight = 0.0f;

    const auto& indices = batch.indices;
    const auto& vertices = batch.vertices;
    const auto max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_verts, max_tris);
    std::vector<meshopt_Meshlet> mlts(max_meshlets);
    std::vector<meshopt_Bounds> mlt_bnds;
    std::vector<uint32_t> mlt_vtxs(max_meshlets * max_verts);
    std::vector<uint8_t> mlt_idxs(max_meshlets * max_tris * 3);

    const auto mltcnt = meshopt_buildMeshlets(mlts.data(), mlt_vtxs.data(), mlt_idxs.data(), indices.data(),
                                              indices.size(), &vertices[0].position.x, vertices.size(),
                                              sizeof(vertices[0]), max_verts, max_tris, cone_weight);

    const auto& last_mlt = mlts.at(mltcnt - 1);
    mlt_vtxs.resize(last_mlt.vertex_offset + last_mlt.vertex_count);
    mlt_idxs.resize(last_mlt.triangle_offset + ((last_mlt.triangle_count * 3 + 3) & ~3));
    mlts.resize(mltcnt);
    mlt_bnds.reserve(mltcnt);
    for(auto& m : mlts)
    {
        meshopt_optimizeMeshlet(&mlt_vtxs.at(m.vertex_offset), &mlt_idxs.at(m.triangle_offset), m.triangle_count, m.vertex_count);
        const auto mbounds =
            meshopt_computeMeshletBounds(&mlt_vtxs.at(m.vertex_offset), &mlt_idxs.at(m.triangle_offset),
                                         m.triangle_count, &vertices[0].position.x, vertices.size(), sizeof(vertices[0]));
        mlt_bnds.push_back(mbounds);
    }
    out_vertices.resize(mlt_vtxs.size());
    std::transform(mlt_vtxs.begin(), mlt_vtxs.end(), out_vertices.begin(),
                   [&vertices](uint32_t idx) { return vertices[idx]; });

    out_indices.resize(mlt_idxs.size());
    std::transform(mlt_idxs.begin(), mlt_idxs.end(), out_indices.begin(),
                   [](auto idx) { return static_cast<uint8_t>(idx); });
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

Handle<MeshPass> Renderer::make_mesh_pass(const MeshPassCreateInfo& info)
{
    auto it = mesh_passes.insert(MeshPass{ .name = info.name, .effects = info.effects });
    return it.handle;
}

Handle<DescriptorPool> Renderer::make_descpool(const DescriptorPoolCreateInfo& info)
{
    descpools.push_back(backend->make_descpool(info));
    return Handle<DescriptorPool>{ (uint32_t)descpools.size() - 1 };
}

void Renderer::update_transform(ecs::entity entity)
{
    if(Engine::get().ecs->get<ecs::Mesh>(entity)) { new_transforms.push_back(entity); }
    if(Engine::get().ecs->get<ecs::Light>(entity)) { new_lights.push_back(entity); }
}

SubmitQueue* Renderer::get_queue(QueueType type) { return backend->get_queue(type); }

uint32_t Renderer::get_bindless(Handle<Buffer> buffer, Range range) { return bindless->get_index(buffer, range); }

uint32_t Renderer::get_bindless(Handle<Texture> texture) { return bindless->get_index(texture); }

uint32_t Renderer::get_bindless(Handle<Sampler> sampler) { return bindless->get_index(sampler); }

uint32_t Renderer::get_perframe_index(int32_t offset)
{
    return (Engine::get().frame_num + perframe.size() + offset) % perframe.size();
}

Renderer::PerFrame& Renderer::get_perframe(int32_t offset) { return perframe.at(get_perframe_index(offset)); }

bool PipelineLayout::is_compatible(const PipelineLayout& a) const
{
    if(info.range != a.info.range) { return false; }

    const size_t set_count = std::min(info.sets.size(), a.info.sets.size());
    for(size_t i = 0; i < set_count; ++i)
    {
        const auto& s1 = info.sets[i];
        const auto& s2 = a.info.sets[i];
        if(s1.flags != s2.flags) { return false; }
        if(s1.bindings.size() != s2.bindings.size()) { return false; }
        for(size_t j = 0; j < s1.bindings.size(); ++j)
        {
            const auto& b1 = s1.bindings[j];
            const auto& b2 = s2.bindings[j];
            if(b1.type != b2.type) { return false; }
            if(b1.slot != b2.slot) { return false; }
            if(b1.size != b2.size) { return false; }
            if(b1.stages != b2.stages) { return false; }
            if(b1.flags != b2.flags) { return false; }
            if(b1.immutable_samplers == nullptr && b2.immutable_samplers == nullptr) {}
            else if(b1.immutable_samplers != nullptr && b2.immutable_samplers != nullptr)
            {
                for(auto i = 0u; i < b1.size; ++i)
                {
                    if(b1.immutable_samplers[i] != b2.immutable_samplers[i]) { return false; }
                }
            }
            else { return false; }
        }
    }
    return true;
}

Handle<DescriptorSet> DescriptorPool::allocate(Handle<PipelineLayout> playout, uint32_t dset_idx)
{
    sets.push_back(Engine::get().renderer->backend->allocate_set(*this, playout.get(), dset_idx));
    return Handle<DescriptorSet>{ (uint32_t)sets.size() - 1 };
}

} // namespace gfx
} // namespace eng