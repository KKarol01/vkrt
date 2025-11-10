#pragma once

#include <eng/renderer/renderer.hpp>
#include <eng/renderer/renderer_vulkan.hpp> // todo: remove this
#include <eng/engine.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/renderer/vulkan_structs.hpp>

namespace eng
{

namespace gfx
{

namespace pass
{

namespace culling
{

class ZPrepass : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        v2::RenderGraph::ResourceView zbufs;
        const std::vector<gfx::Renderer::IndirectBatch>* ibatches;
    };
    ZPrepass(v2::RenderGraph* g, const CreateInfo& info) : Pass("culling::ZPrepass", RenderOrder::DEFAULT_UNLIT)
    {
        auto* r = Engine::get().renderer;
        auto* w = Engine::get().window;
        zbufs = info.zbufs;
        culled_id_bufs =
            g->make_resource(BufferDescriptor{ "cull ids", 1024,
                                               BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT | BufferUsage::CPU_ACCESS },
                             r->frame_count);
        culled_cmd_bufs =
            g->make_resource(BufferDescriptor{ "cull cmds", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT },
                             r->frame_count);
        cullzout_pipeline = r->make_pipeline(PipelineCreateInfo{
            .shaders = { r->make_shader("common/zoutput.vert.glsl"), r->make_shader("common/zoutput.frag.glsl") },
            .layout = r->bindless_pplayout,
            .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
            .depth_test = true,
            .depth_write = true,
            .depth_compare = DepthCompare::GREATER,
            .culling = CullFace::BACK,
        });
    }
    void setup() override
    {
        access(zbufs, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW, ImageLayout::ATTACHMENT, true);
        access(culled_id_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
        access(culled_cmd_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        auto* r = Engine::get().renderer;
        const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
        VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
        VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
        const auto vkdep =
            Vks(VkRenderingAttachmentInfo{ .imageView = rg->get_resource(zbufs).image->default_view->md.vk->view,
                                           .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                           .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                           .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0u } } });
        const auto vkreninfo = Vks(VkRenderingInfo{ .renderArea = vksciss, .layerCount = 1, .pDepthAttachment = &vkdep });
        cmd->set_scissors(&vksciss, 1);
        cmd->set_viewports(&vkview, 1);
        cmd->bind_index(r->bufs.idx_buf.get(), 0, r->bufs.index_type);
        cmd->bind_pipeline(cullzout_pipeline.get());
        cmd->bind_resource(0, r->get_perframe().constants);
        cmd->bind_resource(1, rg->get_resource(culled_id_bufs.get(-1)).buffer);
        cmd->begin_rendering(vkreninfo);
        r->render_ibatch(cmd, (*ibatches)[r->get_perframe_index(-1)], nullptr, false);
        cmd->end_rendering();
    }
    v2::RenderGraph::ResourceView zbufs;
    v2::RenderGraph::ResourceView culled_id_bufs;
    v2::RenderGraph::ResourceView culled_cmd_bufs;
    const std::vector<gfx::Renderer::IndirectBatch>* ibatches; // todo: this shouldn't be here
    Handle<Pipeline> cullzout_pipeline;
};

class Hiz : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        v2::RenderGraph::ResourceView zbufs;
    };
    Hiz(v2::RenderGraph* g, const CreateInfo& info) : Pass("culling::Hiz", RenderOrder::DEFAULT_UNLIT)
    {
        auto* r = Engine::get().renderer;
        auto* w = Engine::get().window;
        zbuf = info.zbufs;
        const auto hizpmips = (uint32_t)(std::log2f(std::max(w->width, w->height)) + 1);
        hiz = g->make_resource(ImageDescriptor{ .name = "hizpyramid",
                                                .width = (uint32_t)w->width,
                                                .height = (uint32_t)w->height,
                                                .mips = (uint32_t)(hizpmips),
                                                .format = ImageFormat::R32F,
                                                .usage = ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT },
                               r->frame_count);
        hiz_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
            .shaders = { Engine::get().renderer->make_shader("culling/hiz.comp.glsl") }, .layout = r->bindless_pplayout });
    }
    void setup() override
    {
        access(zbuf, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
        access(hiz, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, ImageLayout::GENERAL, true);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        auto* r = Engine::get().renderer;
        const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
        auto& hizp = rg->get_resource(hiz).image.get();
        cmd->bind_pipeline(hiz_pipeline.get());
        cmd->bind_resource(4, r->make_texture(TextureDescriptor{ rg->get_resource(zbuf).image->default_view,
                                                                 ImageLayout::GENERAL, false }));
        cmd->bind_resource(5, r->make_texture(TextureDescriptor{
                                  r->make_view(ImageViewDescriptor{ .image = rg->get_resource(hiz).image, .mips = { 0, 1 } }),
                                  ImageLayout::GENERAL, true }));
        cmd->dispatch((hizp.width + 31) / 32, (hizp.height + 31) / 32, 1);
        cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
        for(auto i = 1u; i < hizp.mips; ++i)
        {
            cmd->bind_resource(4, r->make_texture(TextureDescriptor{
                                      r->make_view(ImageViewDescriptor{ .image = rg->get_resource(hiz).image, .mips = { i - 1, 1 } }),
                                      ImageLayout::GENERAL, false }));
            cmd->bind_resource(5, r->make_texture(TextureDescriptor{
                                      r->make_view(ImageViewDescriptor{ .image = rg->get_resource(hiz).image, .mips = { i, 1 } }),
                                      ImageLayout::GENERAL, true }));
            const auto sx = ((hizp.width >> i) + 31) / 32;
            const auto sy = ((hizp.height >> i) + 31) / 32;
            cmd->dispatch(sx, sy, 1);
            cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
        }
    }
    v2::RenderGraph::ResourceView zbuf;
    v2::RenderGraph::ResourceView hiz;
    Handle<Pipeline> hiz_pipeline;
};

class MainPass : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        const gfx::Renderer::RenderPass* fwd;
        const ZPrepass* pzprepass;
        const Hiz* phiz;
    };
    MainPass(v2::RenderGraph* g, const CreateInfo& info) : Pass("culling::MainPass", RenderOrder::DEFAULT_UNLIT)
    {
        auto* r = Engine::get().renderer;
        fwd = info.fwd;
        culled_id_bufs = info.pzprepass->culled_id_bufs;
        culled_cmd_bufs = info.pzprepass->culled_cmd_bufs;
        std::vector<Handle<Buffer>> vfwd_batch_ids(r->frame_count);
        std::vector<Handle<Buffer>> vfwd_batch_cmds(r->frame_count);
        batches.resize(r->frame_count);
        for(auto i = 0u; i < r->frame_count; ++i)
        {
            vfwd_batch_ids[i] = fwd->batch.ids_buf;
            vfwd_batch_cmds[i] = fwd->batch.cmd_buf;
            batches[i].ids_buf = g->get_resource(culled_id_bufs.at(i)).buffer;
            batches[i].cmd_buf = g->get_resource(culled_cmd_bufs.at(i)).buffer;
        }
        fwd_id_bufs = g->import_resource(std::span{ vfwd_batch_ids });
        fwd_cmd_bufs = g->import_resource(std::span{ vfwd_batch_cmds });
        hiz = info.phiz->hiz;
        cull_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
            .shaders = { Engine::get().renderer->make_shader("culling/culling.comp.glsl") },
            .layout = r->bindless_pplayout,
        });
    }
    void setup() override
    {
        access(fwd_id_bufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT);
        access(fwd_cmd_bufs, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
        access(culled_id_bufs, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
               PipelineAccess::SHADER_RW | PipelineAccess::TRANSFER_WRITE_BIT);
        access(culled_cmd_bufs, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
               PipelineAccess::SHADER_RW | PipelineAccess::TRANSFER_WRITE_BIT);
        access(hiz, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
        // access(pf.culling.debug_bsphere->default_view, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
        //        PipelineAccess::SHADER_READ_BIT | PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::GENERAL);
        // access(pf.culling.debug_depth->default_view, PipelineStage::COMPUTE_BIT | PipelineStage::TRANSFER_BIT,
        //        PipelineAccess::SHADER_READ_BIT | PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::GENERAL);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        auto* r = Engine::get().renderer;
        const auto& rp = r->render_passes.at(RenderPassType::FORWARD);
        const auto pfi = r->get_perframe_index();
        batches[pfi].batches = rp.batch.batches;
        batches[pfi].cmd_count = rp.batch.cmd_count;
        batches[pfi].cmd_start = rp.batch.cmd_start;
        batches[pfi].ids_count = rp.batch.ids_count;

        const auto ZERO = 0u;
        r->sbuf->copy(rg->get_resource(culled_cmd_bufs).buffer, rp.batch.cmd_buf, 0, { 0, rp.batch.cmd_buf->size });
        r->sbuf->copy(rg->get_resource(culled_id_bufs).buffer, &ZERO, 0, 4);
        if(rg->get_resource(culled_id_bufs).buffer->capacity < rp.batch.ids_buf->size)
        {
            r->sbuf->resize(rg->get_resource(culled_id_bufs).buffer, rp.batch.ids_buf->size);
        }
        q->wait_sync(r->sbuf->flush(), PipelineStage::COMPUTE_BIT);

        // cmd->clear_color(pf.culling.debug_bsphere.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
        // cmd->clear_color(pf.culling.debug_depth.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
        // cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
        //              PipelineAccess::SHADER_RW);

        cmd->bind_pipeline(cull_pipeline.get());
        cmd->bind_resource(0, r->get_perframe().constants);
        cmd->bind_resource(1, rp.batch.ids_buf);
        cmd->bind_resource(2, rg->get_resource(culled_id_bufs).buffer);
        cmd->bind_resource(3, rg->get_resource(culled_cmd_bufs).buffer, { batches[pfi].cmd_start, ~0ull });
        cmd->bind_resource(4, r->make_texture(TextureDescriptor{ rg->get_resource(hiz).image->default_view, ImageLayout::GENERAL }));
        // cmd->bind_resource(6, make_texture(TextureDescriptor{ pf.culling.debug_bsphere->default_view, ImageLayout::GENERAL, true }));
        // cmd->bind_resource(7, make_texture(TextureDescriptor{ pf.culling.debug_depth->default_view, ImageLayout::GENERAL, true }));
        cmd->dispatch((rp.batch.ids_count + 31) / 32, 1, 1);
    }
    std::vector<gfx::Renderer::IndirectBatch> batches;
    v2::RenderGraph::ResourceView fwd_id_bufs;
    v2::RenderGraph::ResourceView fwd_cmd_bufs;
    v2::RenderGraph::ResourceView culled_id_bufs;
    v2::RenderGraph::ResourceView culled_cmd_bufs;
    v2::RenderGraph::ResourceView hiz;
    const gfx::Renderer::RenderPass* fwd{};
    Handle<Pipeline> cull_pipeline;
};
} // namespace culling

namespace fwdp
{
class LightCulling : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        v2::RenderGraph::ResourceView zbufs;
        uint32_t num_tiles;
        uint32_t lights_per_tile;
        uint32_t tile_pixels;
    };
    LightCulling(v2::RenderGraph* g, const CreateInfo& info) : Pass("fwdp::LightCulling", RenderOrder::DEFAULT_UNLIT)
    {
        num_tiles = info.num_tiles;
        lights_per_tile = info.lights_per_tile;
        tile_pixels = info.tile_pixels;
        auto* r = Engine::get().renderer;
        zbufs = info.zbufs;
        const auto light_list_size = info.num_tiles * info.lights_per_tile * sizeof(uint32_t) + 128;
        const auto light_grid_size = info.num_tiles * 2 * sizeof(uint32_t);
        culled_light_list_bufs =
            g->make_resource(BufferDescriptor{ "fwdp light list", light_list_size, BufferUsage::STORAGE_BIT }, r->frame_count);
        culled_light_grid_bufs =
            g->make_resource(BufferDescriptor{ "fwdp light grid", light_grid_size, BufferUsage::STORAGE_BIT }, r->frame_count);
        light_culling_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
            .shaders = { Engine::get().renderer->make_shader("forwardp/cull_lights.comp.glsl") },
            .layout = r->bindless_pplayout,
        });
    }
    void setup() override
    {
        access(culled_light_list_bufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
        access(culled_light_grid_bufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_RW);
        access(zbufs, PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_READ_BIT, ImageLayout::GENERAL);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        auto* r = Engine::get().renderer;
        cmd->bind_pipeline(light_culling_pipeline.get());
        cmd->bind_resource(0, r->get_perframe().constants);
        cmd->bind_resource(1, rg->get_resource(culled_light_grid_bufs).buffer);
        cmd->bind_resource(2, rg->get_resource(culled_light_list_bufs).buffer);
        cmd->bind_resource(3, r->make_texture(TextureDescriptor{ rg->get_resource(zbufs).image->default_view,
                                                                 ImageLayout::GENERAL, true }));

        const uint32_t zero = 0u;
        r->sbuf->copy(rg->get_resource(culled_light_list_bufs).buffer, &zero, 0ull, 4);
        q->wait_sync(r->sbuf->flush(), PipelineStage::COMPUTE_BIT);

        const auto* w = Engine::get().window;
        auto dx = (uint32_t)w->width;
        auto dy = (uint32_t)w->height;
        dx = (dx + tile_pixels - 1) / tile_pixels;
        dy = (dy + tile_pixels - 1) / tile_pixels;
        cmd->dispatch(dx, dy, 1);
    }
    uint32_t num_tiles;
    uint32_t lights_per_tile;
    uint32_t tile_pixels;
    v2::RenderGraph::ResourceView zbufs;
    v2::RenderGraph::ResourceView culled_light_list_bufs;
    v2::RenderGraph::ResourceView culled_light_grid_bufs;
    Handle<Pipeline> light_culling_pipeline;
};

} // namespace fwdp

class DefaultUnlit : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        const culling::ZPrepass* pzprepass;
        const fwdp::LightCulling* plightculling;
        v2::RenderGraph::ResourceView cbufs;
        v2::RenderGraph::ResourceView zbufs;
    };
    DefaultUnlit(v2::RenderGraph* g, const CreateInfo& info) : Pass("DefaultUnlit", RenderOrder::DEFAULT_UNLIT)
    {
        auto* r = Engine::get().renderer;
        culled_id_bufs = info.pzprepass->culled_id_bufs;
        culled_cmd_bufs = info.pzprepass->culled_cmd_bufs;
        culled_light_list_bufs = info.plightculling->culled_light_list_bufs;
        culled_light_grid_bufs = info.plightculling->culled_light_grid_bufs;
        zbufs = info.zbufs;
        cbufs = info.cbufs;
    }
    void setup() override
    {
        access(culled_id_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
        access(culled_cmd_bufs, PipelineStage::VERTEX_BIT, PipelineAccess::SHADER_READ_BIT);
        access(culled_light_grid_bufs, PipelineStage::FRAGMENT, PipelineAccess::SHADER_READ_BIT);
        access(culled_light_list_bufs, PipelineStage::FRAGMENT, PipelineAccess::SHADER_READ_BIT);
        access(zbufs, PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_READ_BIT, ImageLayout::ATTACHMENT);
        access(cbufs, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, ImageLayout::ATTACHMENT, true);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        auto* r = Engine::get().renderer;
        r->render(RenderPassType::FORWARD, q, cmd);
    }
    v2::RenderGraph::ResourceView culled_id_bufs;
    v2::RenderGraph::ResourceView culled_cmd_bufs;
    v2::RenderGraph::ResourceView culled_light_list_bufs;
    v2::RenderGraph::ResourceView culled_light_grid_bufs;
    v2::RenderGraph::ResourceView zbufs;
    v2::RenderGraph::ResourceView cbufs;
};

class ImGui : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        v2::RenderGraph::ResourceView cbufs;
    };
    ImGui(v2::RenderGraph* g, const CreateInfo& info) : Pass("ImGui", RenderOrder::DEFAULT_UNLIT)
    {
        cbufs = info.cbufs;
    }
    void setup() override
    {
        access(cbufs, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, ImageLayout::ATTACHMENT);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        auto* r = Engine::get().renderer;
        r->imgui_renderer->update(cmd, rg->get_resource(cbufs).image->default_view);
    }
    v2::RenderGraph::ResourceView cbufs;
};

class PresentCopy : public v2::RenderGraph::Pass
{
  public:
    struct CreateInfo
    {
        v2::RenderGraph::ResourceView cbufs;
        v2::RenderGraph::ResourceView swapcbufs;
        const gfx::Swapchain* swapchain;
    };
    PresentCopy(v2::RenderGraph* g, const CreateInfo& info) : Pass("PresentCopy", RenderOrder::DEFAULT_UNLIT)
    {
        cbufs = info.cbufs;
        swapcbufs = info.swapcbufs;
        swapchain = info.swapchain;
    }
    void setup() override
    {
        access(cbufs, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT, ImageLayout::TRANSFER_SRC);
        swapcbufs.set(swapchain->current_index);
        access(swapcbufs, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, ImageLayout::TRANSFER_DST);
    }
    void execute(v2::RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) override
    {
        cmd->copy(swapchain->get_image().get(), rg->get_resource(cbufs).image.get());
    }
    v2::RenderGraph::ResourceView cbufs;
    v2::RenderGraph::ResourceView swapcbufs;
    const gfx::Swapchain* swapchain;
};

} // namespace pass

} // namespace gfx

} // namespace eng