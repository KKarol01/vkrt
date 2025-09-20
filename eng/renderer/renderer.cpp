#pragma once

#include <meshoptimizer/src/meshoptimizer.h>
#include "renderer.hpp"
#include <eng/engine.hpp>
#include <eng/utils.hpp>
#include <eng/camera.hpp>
#include <eng/renderer/staging_buffer.hpp>
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

void Renderer::init(RendererBackend* backend)
{
    // clang-format off
    ENG_SET_HANDLE_DISPATCHER(Buffer, { return &::eng::Engine::get().renderer->buffers.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Image, { return &::eng::Engine::get().renderer->images.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(ImageView, { return &::eng::Engine::get().renderer->image_views.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Geometry, { return &::eng::Engine::get().renderer->geometries.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Mesh, { return &::eng::Engine::get().renderer->meshes.at(*handle); });
    ENG_SET_HANDLE_DISPATCHER(Texture, { return &::eng::Engine::get().renderer->textures.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Material, { return &::eng::Engine::get().renderer->materials.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Shader, { return &::eng::Engine::get().renderer->shaders.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Pipeline, { return &::eng::Engine::get().renderer->pipelines.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(Sampler, { return &::eng::Engine::get().renderer->samplers.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(MeshPass, { return &::eng::Engine::get().renderer->mesh_passes.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(ShaderEffect, { return &::eng::Engine::get().renderer->shader_effects.at(handle); });
    // clang-format on

    this->backend = backend;
    backend->init();

    auto* ew = Engine::get().window;
    gq = backend->get_queue(QueueType::GRAPHICS);
    swapchain = backend->make_swapchain();
    bindless = new BindlessPool{ ((RendererBackendVulkan*)backend)->dev };
    sbuf = new StagingBuffer{};
    sbuf->init(gq, [this](auto buf) { bindless->update_index(buf); });
    perframe.resize(frame_count);
    rgraph = new RenderGraph{};
    rgraph->init(this);
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
            .usage = ImageUsage::DEPTH_BIT | ImageUsage::TRANSFER_RW | ImageUsage::SAMPLED_BIT,
        });
        pf.cmdpool = gq->make_command_pool();
        pf.acq_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("acquire semaphore {}", i) });
        pf.ren_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 0, ENG_FMT("rendering semaphore {}", i) });
        pf.ren_fen = make_sync({ SyncType::FENCE, 1, ENG_FMT("rendering fence {}", i) });
        pf.swp_sem = make_sync({ SyncType::BINARY_SEMAPHORE, 1, ENG_FMT("swap semaphore {}", i) });
        pf.constants = make_buffer(BufferDescriptor{ ENG_FMT("constants_{}", i), 1024, BufferUsage::STORAGE_BIT });

        const auto hizpmips = (uint32_t)(std::log2f(std::max(ew->width, ew->height)) + 1);
        pf.culling.hizpyramid = make_image(ImageDescriptor{
            .name = ENG_FMT("hizpyramid{}", i),
            .width = (uint32_t)ew->width,
            .height = (uint32_t)ew->height,
            .mips = (uint32_t)(hizpmips),
            .format = ImageFormat::R32F,
            .usage = ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT,
        });
        pf.culling.hizptex =
            make_texture(TextureDescriptor{ pf.culling.hizpyramid->default_view, hiz_sampler, ImageLayout::GENERAL });
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
                                                {},
                                                ImageLayout::GENERAL });
        }

        pf.culling.cmd_buf = make_buffer(BufferDescriptor{ ENG_FMT("cull cmds {}", i), 1024,
                                                           BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
        pf.culling.ids_buf =
            make_buffer(BufferDescriptor{ ENG_FMT("cull ids {}", i), 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
    }

    bufs.vpos_buf = make_buffer(BufferDescriptor{ "vertex positions", 1024, BufferUsage::STORAGE_BIT });
    bufs.vattr_buf = make_buffer(BufferDescriptor{ "vertex attributes", 1024, BufferUsage::STORAGE_BIT });
    bufs.idx_buf = make_buffer(BufferDescriptor{ "vertex indices", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDEX_BIT });
    bufs.bsphere_buf = make_buffer(BufferDescriptor{ "bounding spheres", 1024, BufferUsage::STORAGE_BIT });
    for(uint32_t i = 0; i < 2; ++i)
    {
        bufs.trs_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("trs {}", i), 1024, BufferUsage::STORAGE_BIT });
        // bufs.const_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("constants_{}", i), 1024, BufferUsage::STORAGE_BIT });
    }
    for(auto i = 0u; i < (uint32_t)MeshPassType::LAST_ENUM; ++i)
    {
        render_passes.at(i).cmd_buf = make_buffer(BufferDescriptor{ ENG_FMT("{}_cmds", to_string((MeshPassType)i)), 1024,
                                                                    BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
        render_passes.at(i).ids_buf =
            make_buffer(BufferDescriptor{ ENG_FMT("{}_ids", to_string((MeshPassType)i)), 1024, BufferUsage::STORAGE_BIT });
    }

    imgui_renderer = new ImGuiRenderer{};
    imgui_renderer->init();

    const auto pp_default_unlit = make_pipeline(PipelineCreateInfo{
        .shaders = { make_shader("default_unlit/unlit.vert.glsl"), make_shader("default_unlit/unlit.frag.glsl") },
        .attachments = { .count = 1, .color_formats = { ImageFormat::R8G8B8A8_SRGB }, .depth_format = ImageFormat::D32_SFLOAT },
        .depth_test = true,
        .depth_write = true,
        .depth_compare = DepthCompare::GREATER,
        .culling = CullFace::BACK,
    });
    MeshPassCreateInfo info{ .name = "default_unlit" };
    info.effects[(uint32_t)MeshPassType::FORWARD] = make_shader_effect(ShaderEffect{ .pipeline = pp_default_unlit });
    default_meshpass = make_mesh_pass(info);
    default_material = materials.insert(Material{ .mesh_pass = default_meshpass }).handle;

    hiz_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("culling/hiz.comp.glsl") } });
    hiz_sampler = make_sampler(SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE },
        .mipmap_mode = SamplerMipmapMode::NEAREST,
        .reduction_mode = SamplerReductionMode::MIN });
    cull_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("culling/culling.comp.glsl") } });
    cullzout_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
        .shaders = { Engine::get().renderer->make_shader("common/zoutput.vert.glsl"),
                     Engine::get().renderer->make_shader("common/zoutput.frag.glsl") },
        .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
        .depth_test = true,
        .depth_write = true,
        .depth_compare = DepthCompare::GREATER,
        .culling = CullFace::BACK,
    });
}

void Renderer::update()
{
    auto* ew = Engine::get().window;
    const auto pfi = Engine::get().frame_num % Renderer::frame_count;
    auto& pf = get_perframe();
    const auto& ppf = perframe.at((Engine::get().frame_num + perframe.size() - 1) % perframe.size()); // get previous frame res

    if(compile_shaders.size())
    {
        for(auto& e : compile_shaders)
        {
            backend->compile_shader(e.get());
        }
        compile_shaders.clear();
    }
    if(compile_pipelines.size())
    {
        for(auto& e : compile_pipelines)
        {
            backend->compile_pipeline(e.get());
        }
        compile_pipelines.clear();
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

    GPUEngConstantsBuffer cb{
        .vposb = get_bindless(bufs.vpos_buf),
        .vatrb = get_bindless(bufs.vattr_buf),
        .vidxb = get_bindless(bufs.idx_buf),
        .rmbsb = get_bindless(bufs.bsphere_buf),
        .itrsb = get_bindless(bufs.trs_bufs[0]),
        .view = view,
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
    if(pf.culling.ids_buf->capacity < render_passes.at((uint32_t)MeshPassType::FORWARD).ids_buf->size)
    {
        sbuf->resize(pf.culling.ids_buf, render_passes.at((uint32_t)MeshPassType::FORWARD).ids_buf->size);
    }

    gq->wait_sync(sbuf->flush(), PipelineStage::ALL);

    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "culling prepass", RenderOrder::CULLING },
        [&pf, &ppf, this](RenderGraph::PassResourceBuilder& b) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            b.access(ppf.culling.ids_buf, RenderGraph::AccessType::READ_BIT, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
            b.access(pf.culling.hizpyramid->default_view, RenderGraph::AccessType::RW, PipelineStage::EARLY_Z_BIT,
                     PipelineAccess::DS_RW, ImageLayout::GENERAL, true);
            b.access(pf.gbuffer.depth->default_view, RenderGraph::AccessType::RW, PipelineStage::EARLY_Z_BIT,
                     PipelineAccess::DS_RW, ImageLayout::GENERAL);
        },
        [&pf, &ppf, this](SubmitQueue* q, CommandBuffer* cmd) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            auto& hizp = pf.culling.hizpyramid.get();
            pf.culling.mbatches = rp.mbatches;
            cmd->bind_index(bufs.idx_buf.get(), 0, bufs.index_type);
            cmd->bind_pipeline(cullzout_pipeline.get());
            uint32_t pcids[]{ bindless->get_index(pf.constants), bindless->get_index(ppf.culling.ids_buf) };
            cmd->push_constants(ShaderStage::ALL, &pcids, { 0, sizeof(pcids) });
            bindless->bind(cmd);
            VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
            VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
            cmd->set_scissors(&vksciss, 1);
            cmd->set_viewports(&vkview, 1);
            const auto vkdep = Vks(VkRenderingAttachmentInfo{
                .imageView = VkImageViewMetadata::get(pf.gbuffer.depth->default_view.get()).view,
                .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0u } } });
            auto cmdoffacc = 0u;
            const auto vkreninfo = Vks(VkRenderingInfo{ .renderArea = vksciss, .layerCount = 1, .pDepthAttachment = &vkdep });

            const auto dstidscount = 0u;
            sbuf->copy(pf.culling.cmd_buf, rp.cmd_buf, 0, { 0, rp.cmd_buf->size });
            sbuf->copy(pf.culling.ids_buf, &dstidscount, 0, 4u);
            sbuf->copy(pf.culling.ids_buf, rp.ids_buf, 4u, { 4u, rp.ids_buf->size - 4u });

            q->wait_sync(sbuf->flush());

            cmd->begin_rendering(vkreninfo);
            for(auto i = 0u; i < ppf.culling.mbatches.size(); ++i)
            {
                const auto& mb = ppf.culling.mbatches.at(i);
                const auto cntoff = sizeof(uint32_t) * i;
                const auto cmdoff = sizeof(DrawIndirectCommand) * cmdoffacc + rp.cmd_start;
                cmd->draw_indexed_indirect_count(ppf.culling.cmd_buf.get(), cmdoff, ppf.culling.cmd_buf.get(), cntoff,
                                                 mb.instcount, sizeof(DrawIndirectCommand));
                cmdoffacc += mb.instcount;
            }
            cmd->end_rendering();
            cmd->barrier(PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::ALL, PipelineAccess::NONE);
        });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "culling hizpyramid", RenderOrder::CULLING },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            b.access(rp.cmd_buf, RenderGraph::AccessType::READ_BIT, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
            b.access(pf.culling.cmd_buf, RenderGraph::AccessType::WRITE_BIT, PipelineStage::TRANSFER_BIT,
                     PipelineAccess::TRANSFER_WRITE_BIT);

            b.access(pf.culling.ids_buf, RenderGraph::AccessType::RW, PipelineStage::VERTEX_BIT, PipelineAccess::STORAGE_READ_BIT);
            b.access(pf.culling.hizpyramid->default_view, RenderGraph::AccessType::RW, PipelineStage::COMPUTE_BIT,
                     PipelineAccess::SHADER_RW, ImageLayout::GENERAL, true);
            b.access(pf.gbuffer.depth->default_view, RenderGraph::AccessType::READ_BIT, PipelineStage::COMPUTE_BIT,
                     PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            auto& hizp = pf.culling.hizpyramid.get();

            struct PC
            {
                uint32_t engconstsb;
                uint32_t imidb;

                uint32_t hizsampler;
                uint32_t hizsrct2d;
                uint32_t hizdsti;
                uint32_t dstcmdb;
                uint32_t dstimidb;
            } pc;

            cmd->bind_pipeline(hiz_pipeline.get());
            pc.hizsrct2d = bindless->get_index(make_texture(TextureDescriptor{ pf.gbuffer.depth->default_view,
                                                                               hiz_sampler, ImageLayout::GENERAL }));
            pc.hizdsti = bindless->get_index(make_texture(TextureDescriptor{
                make_view(ImageViewDescriptor{ .image = pf.culling.hizpyramid, .mips = { 0, 1 } }), {}, ImageLayout::GENERAL }));
            bindless->update();
            bindless->bind(cmd);
            cmd->push_constants(ShaderStage::ALL, &pc, { 0, sizeof(pc) });
            cmd->dispatch((hizp.width + 31) / 32, (hizp.height + 31) / 32, 1);
            cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
            for(auto i = 1u; i < hizp.mips; ++i)
            {
                pc.hizsrct2d = bindless->get_index(make_texture(TextureDescriptor{
                    make_view(ImageViewDescriptor{ .image = pf.culling.hizpyramid, .mips = { i - 1, 1 } }), hiz_sampler,
                    ImageLayout::GENERAL }));
                pc.hizdsti = bindless->get_index(make_texture(TextureDescriptor{
                    make_view(ImageViewDescriptor{ .image = pf.culling.hizpyramid, .mips = { i, 1 } }), {}, ImageLayout::GENERAL }));
                cmd->push_constants(ShaderStage::ALL, &pc, { 0, sizeof(pc) });
                bindless->update();
                const auto sx = ((hizp.width >> i) + 31) / 32;
                const auto sy = ((hizp.height >> i) + 31) / 32;
                cmd->dispatch(sx, sy, 1);
                cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT,
                             PipelineAccess::SHADER_RW);
            }
        });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "culling main pass", RenderOrder::DEFAULT_UNLIT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            b.access(rp.cmd_buf, RenderGraph::AccessType::READ_BIT, PipelineStage::INDIRECT_BIT, PipelineAccess::INDIRECT_READ_BIT);
            b.access(pf.gbuffer.color->default_view, RenderGraph::AccessType::WRITE_BIT, PipelineStage::ALL,
                     PipelineAccess::NONE, ImageLayout::ATTACHMENT, true);
            b.access(pf.gbuffer.depth->default_view, RenderGraph::AccessType::RW, PipelineStage::EARLY_Z_BIT,
                     PipelineAccess::DS_RW, ImageLayout::ATTACHMENT, true);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            cmd->bind_pipeline(cull_pipeline.get());

            struct PC
            {
                uint32_t engconstsb;
                uint32_t imidb;
                uint32_t hizsampler;
                uint32_t hizsrct2d;
                uint32_t hizdsti;
                uint32_t dstcmdb;
                uint32_t dstimidb;
            } pc;
            pc.engconstsb = bindless->get_index(pf.constants);
            pc.imidb = bindless->get_index(rp.ids_buf);
            pc.dstcmdb = bindless->get_index(pf.culling.cmd_buf, { rp.cmd_start, ~0ull });
            pc.dstimidb = bindless->get_index(pf.culling.ids_buf);
            cmd->push_constants(ShaderStage::ALL, &pc, { 0, sizeof(pc) });
            bindless->bind(cmd);
            cmd->dispatch((rp.id_count + 31) / 32, 1, 1);
        });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "default_unlit", RenderOrder::DEFAULT_UNLIT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            const auto& rp = render_passes.at((uint32_t)MeshPassType::FORWARD);
            b.access(rp.cmd_buf, RenderGraph::AccessType::READ_BIT, PipelineStage::INDIRECT_BIT, PipelineAccess::INDIRECT_READ_BIT);
            b.access(pf.gbuffer.color->default_view, RenderGraph::AccessType::WRITE_BIT, PipelineStage::ALL,
                     PipelineAccess::NONE, ImageLayout::ATTACHMENT, true);
            b.access(pf.gbuffer.depth->default_view, RenderGraph::AccessType::RW, PipelineStage::EARLY_Z_BIT,
                     PipelineAccess::DS_RW, ImageLayout::ATTACHMENT, true);
        },
        [this](SubmitQueue* q, CommandBuffer* cmd) { render(MeshPassType::FORWARD, q, cmd); });
    rgraph->add_pass(
        RenderGraph::PassCreateInfo{ "present copy", RenderOrder::PRESENT },
        [&pf, this](RenderGraph::PassResourceBuilder& b) {
            b.access(pf.gbuffer.color->default_view, RenderGraph::AccessType::READ_BIT, PipelineStage::TRANSFER_BIT,
                     PipelineAccess::TRANSFER_READ_BIT, ImageLayout::TRANSFER_SRC);
            b.access(swapchain->get_view(), RenderGraph::AccessType::WRITE_BIT, PipelineStage::TRANSFER_BIT,
                     PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::TRANSFER_DST);
        },
        [&pf, this](SubmitQueue* q, CommandBuffer* cmd) {
            cmd->copy(swapchain->get_image().get(), pf.gbuffer.color.get());
        });

    rgraph->compile();
    rgraph->render();
    // imgui_renderer->update(cmd, pf.gbuffer.color->default_view);

    auto* cmd = pf.cmdpool->begin();
    cmd->barrier(swapchain->get_image().get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::ALL,
                 PipelineAccess::NONE, ImageLayout::TRANSFER_DST, ImageLayout::PRESENT);
    pf.cmdpool->end(cmd);

    gq->wait_sync(sbuf->flush())
        .wait_sync(pf.acq_sem, PipelineStage::ALL)
        .with_cmd_buf(cmd)
        .signal_sync(pf.swp_sem, PipelineStage::NONE)
        .signal_sync(pf.ren_fen)
        .submit();
    gq->wait_sync(pf.swp_sem, PipelineStage::NONE).present(swapchain);
    gq->wait_idle();
}

void Renderer::render(MeshPassType pass, SubmitQueue* queue, CommandBuffer* cmd)
{
    auto* ew = Engine::get().window;
    auto& pf = get_perframe();

    cmd->bind_index(bufs.idx_buf.get(), 0, bufs.index_type);
    auto& rp = render_passes.at((uint32_t)pass);
    auto cmdoffacc = 0u;
    for(auto i = 0u; i < rp.mbatches.size(); ++i)
    {
        const auto& mb = rp.mbatches.at(i);
        cmd->bind_pipeline(mb.pipeline.get());
        if(i == 0)
        {
            bindless->bind(cmd);
            uint32_t pcids[]{ bindless->get_index(pf.constants), bindless->get_index(pf.culling.ids_buf) };
            cmd->push_constants(ShaderStage::ALL, &pcids, { 0, 8 });

            VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
            VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
            cmd->set_scissors(&vksciss, 1);
            cmd->set_viewports(&vkview, 1);

            const VkRenderingAttachmentInfo vkcols[] = { Vks(VkRenderingAttachmentInfo{
                .imageView = VkImageViewMetadata::get(pf.gbuffer.color->default_view.get()).view,
                .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .color = { .uint32 = {} } } }) };
            const auto vkdep = Vks(VkRenderingAttachmentInfo{
                .imageView = VkImageViewMetadata::get(pf.gbuffer.depth->default_view.get()).view,
                .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0u } } });
            const auto vkreninfo = Vks(VkRenderingInfo{
                .renderArea = vksciss, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = vkcols, .pDepthAttachment = &vkdep });
            cmd->begin_rendering(vkreninfo);
        }
        const auto cntoff = sizeof(uint32_t) * i;
        const auto cmdoff = sizeof(DrawIndirectCommand) * cmdoffacc + rp.cmd_start;
        // cmd->draw_indexed_indirect_count(rp.cmd_buf.get(), cmdoff, rp.cmd_buf.get(), cntoff, mb.instcount,
        //                                  sizeof(DrawIndirectCommand));
        cmd->draw_indexed_indirect_count(pf.culling.cmd_buf.get(), cmdoff, pf.culling.cmd_buf.get(), cntoff,
                                         mb.instcount, sizeof(DrawIndirectCommand));
        cmdoffacc += mb.instcount;
    }
    if(rp.mbatches.size()) { cmd->end_rendering(); }
    rp.entities.clear();
}

void Renderer::process_meshpass(MeshPassType pass)
{
    // todo: maybe sort entities before actually deeming the batch as to-be-sorted; or make the scene order.
    auto& rp = render_passes.at((uint32_t)pass);
    if(!rp.redo) { return; }
    rp.entity_cache = rp.entities;

    std::vector<MeshletInstance> instances;
    std::vector<ecs::entity> new_entities;
    new_entities.reserve(rp.entities.size());
    for(auto i = 0u; i < rp.entities.size(); ++i)
    {
        const auto& e = rp.entities.at(i);
        auto* emsh = Engine::get().ecs->get<ecs::Mesh>(e);
        if(emsh->gpu_resource == ~0u)
        {
            emsh->gpu_resource = gpu_resource_allocator.allocate_slot();
            new_entities.push_back(e);
        }
        for(const auto& esmshh : emsh->meshes)
        {
            const auto& esmsh = esmshh.get();
            const auto& esmshgeo = esmsh.geometry.get();
            for(auto j = 0u; j < esmshgeo.meshlet_range.size; ++j)
            {
                instances.push_back(MeshletInstance{ esmsh.geometry, esmsh.material, emsh->gpu_resource,
                                                     (uint32_t)esmshgeo.meshlet_range.offset + j });
            }
        }
    }

    if(new_entities.size())
    {
        std::swap(bufs.trs_bufs[0], bufs.trs_bufs[1]);
        sbuf->copy(bufs.trs_bufs[0], bufs.trs_bufs[1], 0, { 0, bufs.trs_bufs[1]->size });
        sbuf->insert_barrier();
    }
    for(auto i = 0u; i < new_entities.size(); ++i)
    {
        auto* t = Engine::get().ecs->get<ecs::Transform>(new_entities.at(i));
        auto* m = Engine::get().ecs->get<ecs::Mesh>(new_entities.at(i));
        sbuf->copy(bufs.trs_bufs[0], &t->global, m->gpu_resource * sizeof(t->global), sizeof(t->global));
    }

    rp.mbatches.resize(instances.size());
    std::vector<DrawIndirectCommand> cmds(instances.size());
    std::vector<uint32_t> cmdcnts(instances.size());
    std::vector<GPUInstanceId> gpuids(instances.size());
    std::vector<glm::vec4> gpubbs(instances.size());
    Handle<Pipeline> prev_pp;
    uint32_t cmdi = ~0u;
    uint32_t mlti = ~0u;
    uint32_t batchi = ~0u;
    std::sort(instances.begin(), instances.end(), [this](const auto& a, const auto& b) {
        if(a.material >= b.material) { return false; } // first sort by material
        if(a.meshlet >= b.meshlet) { return false; }   // then sort by geometry
        return true;
    });
    for(auto i = 0u; i < instances.size(); ++i)
    {
        const auto& inst = instances.at(i);
        const auto& geo = inst.geometry.get();
        const auto& mlt = meshlets.at(inst.meshlet);
        const auto& mat = inst.material.get();
        const auto& pp = mat.mesh_pass->effects.at((uint32_t)pass)->pipeline;
        if(pp != prev_pp)
        {
            prev_pp = pp;
            ++batchi;
            rp.mbatches.at(batchi).pipeline = pp;
        }
        if(inst.meshlet != mlti)
        {
            mlti = inst.meshlet;
            ++cmdi;
            cmds.at(cmdi) = DrawIndirectCommand{ .indexCount = mlt.index_count,
                                                 .instanceCount = 0,
                                                 .firstIndex = (uint32_t)geo.index_range.offset + mlt.index_offset,
                                                 .vertexOffset = (int32_t)(geo.vertex_range.offset + mlt.vertex_offset),
                                                 .firstInstance = i };
        }
        gpuids.at(i) = GPUInstanceId{ .batch_id = cmdi, .residx = inst.meshlet, .instidx = inst.gpu_resource };
        gpubbs.at(i) = mlt.bounding_sphere; // fix bbs to be per meshlet and indexable by gpuinstance id - or not...
        ++rp.mbatches.at(batchi).instcount;
        rp.mbatches.at(batchi).cmdcount = cmdi + 1; // todo: use it
        ++cmdcnts.at(batchi);
        //++cmds.at(cmdi).instanceCount;
    }
    ++cmdi;
    ++batchi;
    cmds.resize(cmdi);
    cmdcnts.resize(batchi);
    rp.mbatches.resize(batchi);
    const auto post_cull_tri_count = 0u;
    const auto mltcount = (uint32_t)instances.size();
    const auto cmdoff = align_up2(batchi * sizeof(uint32_t), 16ull);
    rp.cmd_count = cmdi;
    rp.id_count = gpuids.size();
    rp.redo = false;
    rp.cmd_start = cmdoff;
    sbuf->copy(rp.cmd_buf, cmdcnts, 0);
    sbuf->copy(rp.cmd_buf, cmds, cmdoff);
    sbuf->copy(rp.ids_buf, &rp.id_count, 0, sizeof(rp.id_count));
    sbuf->copy(rp.ids_buf, gpuids, sizeof(rp.id_count));
}

void Renderer::submit_mesh(const SubmitInfo& info)
{
    auto& rp = render_passes.at((uint32_t)info.type);
    const auto idx = rp.entities.size();
    rp.entities.push_back(info.entity);
    rp.redo = (rp.entities.size() > rp.entity_cache.size()) ||
              (rp.entity_cache.size() > idx && rp.entities.at(idx) != rp.entity_cache.at(idx));
}

Handle<Buffer> Renderer::make_buffer(const BufferDescriptor& info)
{
    return buffers.insert(backend->make_buffer(info));
}

Handle<Image> Renderer::make_image(const ImageDescriptor& info)
{
    auto h = images.insert(backend->make_image(info));
    h->default_view = make_view(ImageViewDescriptor{ .name = ENG_FMT("{}_default", info.name), .image = h });
    if(info.data.size_bytes()) { sbuf->copy(h, info.data.data(), ImageLayout::READ_ONLY); }
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
    if(it.success) { compile_shaders.push_back(it.handle); }
    return it.handle;
}

Handle<Pipeline> Renderer::make_pipeline(const PipelineCreateInfo& info)
{
    Pipeline p{ .info = info };
    auto it = pipelines.insert(std::move(p));
    if(it.success) { compile_pipelines.push_back(it.handle); }
    return it.handle;
}

Sync* Renderer::make_sync(const SyncCreateInfo& info) { return backend->make_sync(info); }

Handle<Texture> Renderer::make_texture(const TextureDescriptor& batch)
{
    return textures.insert(Texture{ batch.view, batch.sampler, batch.layout }).handle;
}

Handle<Material> Renderer::make_material(const MaterialDescriptor& desc)
{
    auto meshpass = mesh_passes.find(MeshPass{ desc.mesh_pass });
    if(!meshpass) { meshpass = default_meshpass; }
    return materials.insert(Material{ .mesh_pass = meshpass, .base_color_texture = desc.base_color_texture }).handle;
}

Handle<Geometry> Renderer::make_geometry(const GeometryDescriptor& batch)
{
    std::vector<Vertex> out_vertices;
    std::vector<uint16_t> out_indices;
    std::vector<Meshlet> out_meshlets;
    meshletize_geometry(batch, out_vertices, out_indices, out_meshlets);

    Geometry geometry{ .vertex_range = { bufs.vertex_count, out_vertices.size() },
                       .index_range = { bufs.index_count, out_indices.size() },
                       .meshlet_range = { meshlets.size(), out_meshlets.size() } };

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
    std::vector<meshopt_Meshlet> meshlets(max_meshlets);
    std::vector<meshopt_Bounds> meshlets_bounds;
    std::vector<uint32_t> meshlets_verts(max_meshlets * max_verts);
    std::vector<uint8_t> meshlets_triangles(max_meshlets * max_tris * 3);

    const auto meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshlets_verts.data(), meshlets_triangles.data(),
                                                     indices.data(), indices.size(), &vertices[0].position.x,
                                                     vertices.size(), sizeof(vertices[0]), max_verts, max_tris, cone_weight);

    const auto& last_meshlet = meshlets.at(meshlet_count - 1);
    meshlets_verts.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
    meshlets_triangles.resize(last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3));
    meshlets.resize(meshlet_count);
    meshlets_bounds.reserve(meshlet_count);

    for(auto& m : meshlets)
    {
        meshopt_optimizeMeshlet(&meshlets_verts.at(m.vertex_offset), &meshlets_triangles.at(m.triangle_offset),
                                m.triangle_count, m.vertex_count);
        const auto mbounds =
            meshopt_computeMeshletBounds(&meshlets_verts.at(m.vertex_offset), &meshlets_triangles.at(m.triangle_offset),
                                         m.triangle_count, &vertices[0].position.x, vertices.size(), sizeof(vertices[0]));
        meshlets_bounds.push_back(mbounds);
    }

    out_vertices.resize(meshlets_verts.size());
    std::transform(meshlets_verts.begin(), meshlets_verts.end(), out_vertices.begin(),
                   [&vertices](uint32_t idx) { return vertices[idx]; });

    out_indices.resize(meshlets_triangles.size());
    std::transform(meshlets_triangles.begin(), meshlets_triangles.end(), out_indices.begin(),
                   [](auto idx) { return static_cast<uint16_t>(idx); });
    out_meshlets.resize(meshlet_count);
    for(auto i = 0u; i < meshlet_count; ++i)
    {
        const auto& m = meshlets.at(i);
        const auto& mb = meshlets_bounds.at(i);
        out_meshlets.at(i) = Meshlet{ .vertex_offset = m.vertex_offset,
                                      .vertex_count = m.vertex_count,
                                      .index_offset = m.triangle_offset,
                                      .index_count = m.triangle_count * 3,
                                      .bounding_sphere = glm::vec4{ mb.center[0], mb.center[1], mb.center[2], mb.radius } };
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

void Renderer::update_transform(ecs::entity entity) { ENG_TODO(); }

SubmitQueue* Renderer::get_queue(QueueType type) { return backend->get_queue(type); }

uint32_t Renderer::get_bindless(Handle<Buffer> buffer) { return bindless->get_index(buffer); }

Renderer::PerFrame& Renderer::get_perframe() { return perframe.at(Engine::get().frame_num % perframe.size()); }

} // namespace gfx
} // namespace eng