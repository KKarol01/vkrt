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
#include <assets/shaders/bindless_structures.glsli>

namespace eng
{

namespace gfx
{

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
    ENG_SET_HANDLE_DISPATCHER(Buffer,           { return &::eng::Engine::get().renderer->buffers.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Image,            { return &::eng::Engine::get().renderer->images.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(ImageView,        { return &::eng::Engine::get().renderer->image_views.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Geometry,         { return &::eng::Engine::get().renderer->geometries.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Mesh,             { return &::eng::Engine::get().renderer->meshes.at(*handle); });
    ENG_SET_HANDLE_DISPATCHER(Texture,          { return &::eng::Engine::get().renderer->textures.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Material,         { return &::eng::Engine::get().renderer->materials.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Shader,           { return &::eng::Engine::get().renderer->shaders.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(PipelineLayout,   { return &::eng::Engine::get().renderer->pplayouts.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Pipeline,         { return &::eng::Engine::get().renderer->pipelines.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Sampler,          { return &::eng::Engine::get().renderer->samplers.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(MeshPass,         { return &::eng::Engine::get().renderer->mesh_passes.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(ShaderEffect,     { return &::eng::Engine::get().renderer->shader_effects.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(DescriptorPool,   { return &::eng::Engine::get().renderer->descpools.at(*handle); });
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

    imgui_renderer = new ImGuiRenderer{};
    imgui_renderer->init();
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
    fwdp_gen_frust_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("forwardp/gen_frusts.comp.glsl") },
        .layout = bindless_pplayout,
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
    info.effects[(uint32_t)MeshPassType::FORWARD] = make_shader_effect(ShaderEffect{ .pipeline = default_unlit_pipeline });
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

        const auto hizpmips = (uint32_t)(std::log2f(std::max(ew->width, ew->height)) + 1);
        pf.culling.hizpyramid = make_image(ImageDescriptor{
            .name = ENG_FMT("hizpyramid{}", i),
            .width = (uint32_t)ew->width,
            .height = (uint32_t)ew->height,
            .mips = (uint32_t)(hizpmips),
            .format = ImageFormat::R32F,
            .usage = ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT,
        });
        pf.culling.hizptex = make_texture(TextureDescriptor{ pf.culling.hizpyramid->default_view, ImageLayout::GENERAL });
        pf.culling.hizpmiptexs.resize(hizpmips);
        for(auto i = 0u; i < hizpmips; ++i)
        {
            pf.culling.hizpmiptexs.at(i) =
                make_texture(TextureDescriptor{ make_view(ImageViewDescriptor{ ENG_FMT("hizpmip{}", i),
                                                                               pf.culling.hizpyramid,
                                                                               ImageViewType::TYPE_2D,
                                                                               pf.culling.hizpyramid->format,
                                                                               ImageAspect::COLOR,
                                                                               { i, 1 },
                                                                               { 0, 1 } }),

                                                ImageLayout::GENERAL, true });
        }

        pf.culling.cmd_buf = make_buffer(BufferDescriptor{ ENG_FMT("cull cmds {}", i), 1024,
                                                           BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
        pf.culling.ids_buf =
            make_buffer(BufferDescriptor{ ENG_FMT("cull ids {}", i), 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
        pf.culling.debug_bsphere = make_image(ImageDescriptor{
            .name = ENG_FMT("debug_bsphere{}", i),
            .width = (uint32_t)ew->width,
            .height = (uint32_t)ew->height,
            .mips = 1,
            .format = ImageFormat::R32FG32FB32FA32F,
            .usage = ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT,
        });
        pf.culling.debug_depth = make_image(ImageDescriptor{
            .name = ENG_FMT("debug_depth{}", i),
            .width = (uint32_t)ew->width,
            .height = (uint32_t)ew->height,
            .mips = 1,
            .format = ImageFormat::R32FG32FB32FA32F,
            .usage = ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT,
        });

        {
            const auto* w = Engine::get().window;
            const auto light_list_size = bufs.fwdp_num_tiles * bufs.fwdp_lights_per_tile * sizeof(uint32_t) + sizeof(uint32_t);
            const auto light_grid_size = bufs.fwdp_num_tiles * 2 * sizeof(uint32_t);
            pf.fwdp.light_list_buf =
                make_buffer(BufferDescriptor{ ENG_FMT("fwdp light list {}", i), light_list_size, BufferUsage::STORAGE_BIT });
            pf.fwdp.light_grid_buf =
                make_buffer(BufferDescriptor{ ENG_FMT("fwdp light grid {}", i), light_grid_size, BufferUsage::STORAGE_BIT });
        }
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
    for(auto i = 0u; i < (uint32_t)MeshPassType::LAST_ENUM; ++i)
    {
        render_passes.at(i).cmd_buf = make_buffer(BufferDescriptor{ ENG_FMT("{}_cmds", to_string((MeshPassType)i)), 1024,
                                                                    BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
        render_passes.at(i).ids_buf =
            make_buffer(BufferDescriptor{ ENG_FMT("{}_ids", to_string((MeshPassType)i)), 1024, BufferUsage::STORAGE_BIT });
    }

    {
        const auto* w = Engine::get().window;
        const auto num_tiles_x = (uint32_t)std::ceilf(w->width / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles_y = (uint32_t)std::ceilf(w->height / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles = num_tiles_x * num_tiles_y;
        const auto size = num_tiles * sizeof(GPUFWDPFrustum);
        bufs.fwdp_frustums_buf = make_buffer(BufferDescriptor{ "fwdp_frustums", size, BufferUsage::STORAGE_BIT });
        bufs.fwdp_num_tiles = num_tiles;
    }
}

void Renderer::update()
{
    auto* ew = Engine::get().window;
    const auto pfi = Engine::get().frame_num % Renderer::frame_count;
    auto& pf = get_perframe();
    const auto& ppf = perframe.at((Engine::get().frame_num + perframe.size() - 1) % perframe.size()); // get previous frame res

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
            if(l->gpu_index == ~0u)
            {
                l->gpu_index = lights.size();
                lights.push_back(new_lights.at(i));
            }
            GPULight gpul{ t->pos(), l->range, l->color, l->intensity, (uint32_t)l->type };
            sbuf->copy(bufs.lights_bufs[0], &gpul,
                       offsetof(GPULightsBuffer, lights_us) + l->gpu_index * sizeof(GPULight), sizeof(GPULight));
        }
        const auto lc = (uint32_t)lights.size();
        sbuf->copy(bufs.lights_bufs[0], &lc, 0, 4);
        new_lights.clear();
    }

    pf.ren_fen->wait_cpu(~0ull);
    pf.ren_fen->reset();
    pf.acq_sem->reset();
    pf.ren_sem->reset();
    pf.swp_sem->reset();
    pf.cmdpool->reset();
    swapchain->acquire(~0ull, pf.acq_sem);

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
    };
    sbuf->copy(pf.constants, &cb, 0ull, sizeof(cb));

    for(auto i = 0u; i < (uint32_t)MeshPassType::LAST_ENUM; ++i)
    {
        process_meshpass((MeshPassType)i);
    }
    auto& fwdrp = render_passes[(uint32_t)MeshPassType::FORWARD];
    const auto ZERO = 0u;
    sbuf->copy(pf.culling.cmd_buf, fwdrp.cmd_buf, 0, { 0, fwdrp.cmd_buf->size });
    sbuf->copy(pf.culling.ids_buf, &ZERO, 0, 4);

    if(pf.culling.ids_buf->capacity < render_passes.at((uint32_t)MeshPassType::FORWARD).ids_buf->size)
    {
        sbuf->resize(pf.culling.ids_buf, render_passes.at((uint32_t)MeshPassType::FORWARD).ids_buf->size);
    }

    {
        const uint32_t zero = 0u;
        sbuf->copy(pf.fwdp.light_list_buf, &zero, 0ull, 4);
    }

    if(true || glfwGetKey(Engine::get().window->window, GLFW_KEY_EQUAL) == GLFW_PRESS)
    {
        rgraph->add_pass(
            RenderGraph::PassCreateInfo{ "culling prepass", RenderOrder::DEFAULT_UNLIT },
            [&pf, &ppf, this](RenderGraph::PassResourceBuilder& b) {
                const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
                b.access(ppf.culling.ids_buf, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
                b.access(ppf.culling.cmd_buf, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
                b.access(pf.gbuffer.depth->default_view, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW,
                         ImageLayout::ATTACHMENT, true);
            },
            [&pf, &ppf, this](SubmitQueue* q, CommandBuffer* cmd) {
                const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
                VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
                VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
                const auto vkdep =
                    Vks(VkRenderingAttachmentInfo{ .imageView = pf.gbuffer.depth->default_view->md.vk->view,
                                                   .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                                   .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                   .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0u } } });
                const auto vkreninfo = Vks(VkRenderingInfo{ .renderArea = vksciss, .layerCount = 1, .pDepthAttachment = &vkdep });
                cmd->set_scissors(&vksciss, 1);
                cmd->set_viewports(&vkview, 1);
                cmd->bind_index(bufs.idx_buf.get(), 0, bufs.index_type);
                cmd->bind_pipeline(cullzout_pipeline.get());
                cmd->bind_resource(0, pf.constants);
                cmd->bind_resource(1, ppf.culling.ids_buf);
                cmd->begin_rendering(vkreninfo);
                render_mbatches(cmd, ppf.culling.mbatches, ppf.culling.cmd_buf, ppf.culling.cmd_buf,
                                ppf.culling.cmd_start, 0ull, {}, false);
                cmd->end_rendering();
            });
    }
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "culling hizpyramid", RenderOrder::DEFAULT_UNLIT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            b.access(pf.gbuffer.depth->default_view, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT,
                     ImageLayout::READ_ONLY);
            b.access(pf.culling.hizpyramid->default_view, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW,
                     ImageLayout::GENERAL, true);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            auto& hizp = pf.culling.hizpyramid.get();
            cmd->bind_pipeline(hiz_pipeline.get());
            cmd->bind_resource(4, make_texture(TextureDescriptor{ pf.gbuffer.depth->default_view, ImageLayout::READ_ONLY, false }));
            cmd->bind_resource(5, make_texture(TextureDescriptor{
                                      make_view(ImageViewDescriptor{ .image = pf.culling.hizpyramid, .mips = { 0, 1 } }),
                                      ImageLayout::GENERAL, true }));
            cmd->dispatch((hizp.width + 31) / 32, (hizp.height + 31) / 32, 1);
            cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
            for(auto i = 1u; i < hizp.mips; ++i)
            {
                cmd->bind_resource(4, make_texture(TextureDescriptor{
                                          make_view(ImageViewDescriptor{ .image = pf.culling.hizpyramid, .mips = { i - 1, 1 } }),
                                          ImageLayout::GENERAL, false }));
                cmd->bind_resource(5, make_texture(TextureDescriptor{
                                          make_view(ImageViewDescriptor{ .image = pf.culling.hizpyramid, .mips = { i, 1 } }),
                                          ImageLayout::GENERAL, true }));
                const auto sx = ((hizp.width >> i) + 31) / 32;
                const auto sy = ((hizp.height >> i) + 31) / 32;
                cmd->dispatch(sx, sy, 1);
                cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT,
                             PipelineAccess::SHADER_RW);
            }
        });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "fwdp cull lights", RenderOrder::DEFAULT_UNLIT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            b.access(bufs.fwdp_frustums_buf, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT);
            b.access(pf.fwdp.light_list_buf, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
            b.access(pf.fwdp.light_grid_buf, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
            b.access(pf.gbuffer.depth->default_view, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT,
                     ImageLayout::GENERAL);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            cmd->bind_pipeline(fwdp_cull_lights_pipeline.get());
            cmd->bind_resource(0, pf.constants);
            cmd->push_constants(ShaderStage::ALL, &bufs.fwdp_tile_pixels, { 4, 4 });
            cmd->push_constants(ShaderStage::ALL, &bufs.fwdp_lights_per_tile, { 8, 4 });
            cmd->push_constants(ShaderStage::ALL, &bufs.fwdp_num_tiles, { 12, 4 });
            cmd->bind_resource(4, bufs.fwdp_frustums_buf);
            cmd->bind_resource(5, pf.fwdp.light_grid_buf);
            cmd->bind_resource(6, pf.fwdp.light_list_buf);
            cmd->bind_resource(7, make_texture(TextureDescriptor{ pf.gbuffer.depth->default_view, ImageLayout::GENERAL, true }));
            const auto* w = Engine::get().window;
            auto dx = (uint32_t)w->width;
            auto dy = (uint32_t)w->height;
            dx = (dx + bufs.fwdp_tile_pixels - 1) / bufs.fwdp_tile_pixels; // go over all
            // the pixels in 16x16 workgroups dy = (dy + bufs.fwdp_tile_pixels - 1) / bufs.fwdp_tile_pixels;
            // cmd->dispatch(1, 1, 1);
        });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "culling main pass", RenderOrder::DEFAULT_UNLIT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            b.access(rp.ids_buf, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT);
            b.access(rp.cmd_buf, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
            b.access(pf.culling.cmd_buf, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
                     PipelineAccess::SHADER_RW | PipelineAccess::TRANSFER_WRITE_BIT);
            b.access(pf.culling.ids_buf, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
                     PipelineAccess::SHADER_RW | PipelineAccess::TRANSFER_WRITE_BIT);
            b.access(pf.culling.hizptex->view, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
            b.access(pf.culling.debug_bsphere->default_view, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
                     PipelineAccess::SHADER_READ_BIT | PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::GENERAL);
            b.access(pf.culling.debug_depth->default_view, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
                     PipelineAccess::SHADER_READ_BIT | PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::GENERAL);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            pf.culling.mbatches = rp.mbatches;
            pf.culling.cmd_start = rp.cmd_start;
            pf.culling.cmd_count = rp.cmd_count;
            pf.culling.id_count = rp.id_count;

            cmd->clear_color(pf.culling.debug_bsphere.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
            cmd->clear_color(pf.culling.debug_depth.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
            cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                         PipelineAccess::SHADER_RW);

            cmd->bind_pipeline(cull_pipeline.get());
            cmd->bind_resource(0, pf.constants);
            cmd->bind_resource(1, rp.ids_buf);
            cmd->bind_resource(2, pf.culling.ids_buf);
            cmd->bind_resource(3, pf.culling.cmd_buf, { pf.culling.cmd_start, ~0ull });
            cmd->bind_resource(4, pf.culling.hizptex);
            cmd->bind_resource(6, make_texture(TextureDescriptor{ pf.culling.debug_bsphere->default_view, ImageLayout::GENERAL, true }));
            cmd->bind_resource(7, make_texture(TextureDescriptor{ pf.culling.debug_depth->default_view, ImageLayout::GENERAL, true }));
            cmd->dispatch((rp.id_count + 31) / 32, 1, 1);
        });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "default_unlit", RenderOrder::DEFAULT_UNLIT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            b.access(pf.culling.cmd_buf, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
            b.access(pf.culling.ids_buf, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
            b.access(pf.gbuffer.color->default_view, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT,
                     ImageLayout::ATTACHMENT, true);
            b.access(pf.gbuffer.depth->default_view, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_READ_BIT, ImageLayout::ATTACHMENT);
            b.access(pf.fwdp.light_grid_buf, PipelineStage::FRAGMENT, PipelineAccess::SHADER_READ_BIT);
            b.access(pf.fwdp.light_list_buf, PipelineStage::FRAGMENT, PipelineAccess::SHADER_READ_BIT);
        },
        [this](SubmitQueue* q, CommandBuffer* cmd) { render(MeshPassType::FORWARD, q, cmd); });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "imgui", RenderOrder::PRESENT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            b.access(pf.gbuffer.color->default_view, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT,
                     ImageLayout::ATTACHMENT);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) { imgui_renderer->update(cmd, pf.gbuffer.color->default_view); });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "present copy", RenderOrder::PRESENT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            b.access(pf.gbuffer.color->default_view, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT,
                     ImageLayout::TRANSFER_SRC);
            b.access(swapchain->get_view(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::TRANSFER_DST);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            cmd->copy(swapchain->get_image().get(), pf.gbuffer.color.get());
            // cmd->copy(swapchain->get_image().get(), pf.culling.debug_bsphere.get());
        });

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
    sbuf->reset();
}

void Renderer::render(MeshPassType pass, SubmitQueue* queue, CommandBuffer* cmd)
{
    auto* ew = Engine::get().window;
    auto& pf = get_perframe();
    auto& rp = render_passes.at((uint32_t)pass);

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
    render_mbatches(cmd, rp.mbatches, pf.culling.cmd_buf, pf.culling.cmd_buf, pf.culling.cmd_start, 0ull,
                    [this, &pf](CommandBuffer* cmd) {
                        cmd->bind_resource(0, pf.constants);
                        cmd->bind_resource(1, pf.culling.ids_buf);
                        cmd->bind_resource(2, pf.fwdp.light_grid_buf);
                        cmd->bind_resource(3, bufs.fwdp_frustums_buf);
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
    rp.entities.clear();
}

void Renderer::process_meshpass(MeshPassType pass)
{
    // todo: maybe sort entities before actually deeming the batch as to-be-sorted; or make the scene order.
    auto& rp = render_passes.at((uint32_t)pass);
    // if(!rp.redo) { return; }
    rp.entity_cache = rp.entities;

    std::vector<MeshletInstance> insts;
    std::vector<ecs::entity> newinsts;
    newinsts.reserve(rp.entities.size());
    for(auto i = 0u; i < rp.entities.size(); ++i)
    {
        const auto& ent = rp.entities.at(i);
        auto* entmsh = Engine::get().ecs->get<ecs::Mesh>(ent);
        if(entmsh->gpu_resource == ~0u)
        {
            entmsh->gpu_resource = gpu_resource_allocator.allocate_slot();
            newinsts.push_back(ent);
            new_transforms.push_back(ent);
        }
        for(auto i = 0u; i < entmsh->meshes.size(); ++i)
        {
            const auto& msh = entmsh->meshes.at(i).get();
            const auto& geom = msh.geometry.get();
            for(auto j = 0u; j < geom.meshlet_range.size; ++j)
            {
                insts.push_back(MeshletInstance{ msh.geometry, msh.material, entmsh->gpu_resource, geom.meshlet_range.offset + j });
            }
        }
    }

    rp.mbatches.resize(insts.size());
    std::vector<DrawIndexedIndirectCommand> cmds(insts.size());
    std::vector<uint32_t> cmdcnts(insts.size());
    std::vector<GPUInstanceId> gpuids(insts.size());
    // std::vector<glm::vec4> gpubbs(insts.size());
    Handle<Pipeline> ppp;
    uint32_t cmdi = ~0u;
    uint32_t mlti = ~0u;
    uint32_t batchi = ~0u;
    std::sort(insts.begin(), insts.end(), [this](const auto& a, const auto& b) {
        if(a.material >= b.material) { return false; } // first sort by material
        if(a.meshlet >= b.meshlet) { return false; }   // then sort by geometry
        return true;
    });
    for(auto i = 0u; i < insts.size(); ++i)
    {
        const auto& inst = insts.at(i);
        const auto& geom = inst.geometry.get();
        const auto& mlt = meshlets.at(inst.meshlet);
        const auto& mat = inst.material.get();
        const auto& pp = mat.mesh_pass->effects.at((uint32_t)pass)->pipeline;
        if(pp != ppp)
        {
            ppp = pp;
            ++batchi;
            rp.mbatches.at(batchi).pipeline = pp;
        }
        if(inst.meshlet != mlti)
        {
            mlti = inst.meshlet;
            ++cmdi;
            cmds.at(cmdi) = DrawIndexedIndirectCommand{ .indexCount = mlt.index_count,
                                                        .instanceCount = 0,
                                                        .firstIndex = mlt.index_offset,
                                                        .vertexOffset = mlt.vertex_offset,
                                                        .firstInstance = i };
        }
        gpuids.at(i) = GPUInstanceId{ .cmdi = cmdi, .resi = inst.meshlet, .insti = inst.gpu_resource, .mati = *inst.material };
        // gpubbs.at(i) = mlt.bounding_sphere; // fix bbs to be per meshlet and indexable by gpuinstance id - or not...
        ++rp.mbatches.at(batchi).instcount;
        // cmds.at(cmdi).instanceCount++;
        rp.mbatches.at(batchi).cmdcount = cmdi + 1;
        ++cmdcnts.at(batchi);
    }
    ++cmdi;
    ++batchi;
    cmds.resize(cmdi);
    cmdcnts.resize(batchi);
    rp.mbatches.resize(batchi);
    const auto post_cull_tri_count = 0u;
    const auto mltcount = (uint32_t)insts.size();
    const auto cmdoff = align_up2(cmdcnts.size() * sizeof(uint32_t), 16ull);
    rp.cmd_count = cmdi;
    rp.id_count = gpuids.size();
    rp.redo = false;
    rp.cmd_start = cmdoff;
    // sbuf->copy(rp.cmd_buf, &rp.cmd_count, 0, 4);
    sbuf->copy(rp.cmd_buf, cmdcnts, 0);
    sbuf->copy(rp.cmd_buf, cmds, cmdoff);
    sbuf->copy(rp.ids_buf, &rp.id_count, 0, 4);
    sbuf->copy(rp.ids_buf, gpuids, 4);
    // sbuf->copy(bufs.bsphere_buf, gpubbs, 0);
}

void Renderer::submit_mesh(const SubmitInfo& info)
{
    auto& rp = render_passes.at((uint32_t)info.type);
    const auto idx = rp.entities.size();
    rp.entities.push_back(info.entity);
    // if any entity at any position is mismatched - redo.
    rp.redo |= rp.entity_cache.size() <= idx || rp.entities.at(idx) != rp.entity_cache.at(idx);
}

void Renderer::add_light(ecs::entity light) { new_lights.push_back(light); }

void Renderer::render_mbatches(CommandBuffer* cmd, const std::vector<MultiBatch>& mbatches, Handle<Buffer> indirect,
                               Handle<Buffer> count, size_t cmdoffset, size_t cntoffset,
                               const Callback<void(CommandBuffer*)>& setup_resources, bool bind_pps)
{
    auto cmdoffacc = 0u;
    for(auto i = 0u; i < mbatches.size(); ++i)
    {
        const auto& mb = mbatches.at(i);
        const auto cntoff = sizeof(uint32_t) * i + cntoffset;
        const auto cmdoff = sizeof(DrawIndexedIndirectCommand) * cmdoffacc + cmdoffset;
        if(bind_pps) { cmd->bind_pipeline(mb.pipeline.get()); }
        if(i == 0 && setup_resources) { setup_resources(cmd); }
        cmd->draw_indexed_indirect_count(indirect.get(), cmdoff, count.get(), cntoff, mb.cmdcount, sizeof(DrawIndexedIndirectCommand));
        cmdoffacc += mb.cmdcount;
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
            sbuf->barrier(h, ImageLayout::TRANSFER_SRC, ImageSubRange{ { i, 1 }, { 0, 1 } });
            sbuf->barrier(h, ImageLayout::TRANSFER_DST, ImageSubRange{ { i + 1, 1 }, { 0, 1 } });
            sbuf->blit(h, h, ImageBlit{ { i, { 0, 1 } }, { i + 1, { 0, 1 } }, srcsz, dstsz });
        }
        sbuf->barrier(h, ImageLayout::READ_ONLY);
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
    auto it = image_views.insert(view);
    if(it.success) { backend->make_view(it.handle.get()); }
    info.image->views.push_back(it.handle);
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

    auto it = shaders.insert(Shader{ eng::paths::canonize_path(eng::paths::SHADERS_DIR / path), stage });
    if(it.success) { new_shaders.push_back(it.handle); }
    return it.handle;
}

Handle<PipelineLayout> Renderer::make_pplayout(const PipelineLayoutCreateInfo& info)
{
    PipelineLayout layout{ .info = info };
    auto it = pplayouts.insert(std::move(layout));
    if(it.success) { backend->compile_pplayout(it.handle.get()); }
    return it.handle;
}

Handle<Pipeline> Renderer::make_pipeline(const PipelineCreateInfo& info)
{
    Pipeline p{ .info = info };
    auto it = pipelines.insert(std::move(p));
    if(it.success) { new_pipelines.push_back(it.handle); }
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

Renderer::PerFrame& Renderer::get_perframe() { return perframe.at(Engine::get().frame_num % perframe.size()); }

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