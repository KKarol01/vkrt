#pragma once

#include <eng/renderer/renderer.hpp>
#include <eng/renderer/renderer_vulkan.hpp> // todo: remove this
#include <eng/engine.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/common/hash.hpp>

namespace eng
{

namespace gfx
{

namespace pass
{

class IPass
{
  public:
    virtual ~IPass() = default;
    virtual void init() = 0;
    virtual void on_render_graph(RenderGraph& graph) = 0;
};

class SSTriangle : public IPass
{
    struct SSTrianglePass
    {
        Handle<RenderGraph::ResourceAccess> color;
    };
    struct SSCopyToSwapPass
    {
        Handle<RenderGraph::ResourceAccess> color;
        Handle<RenderGraph::ResourceAccess> swap;
    };
    struct SSSwapPresent
    {
        Handle<RenderGraph::ResourceAccess> swap;
    };

  public:
    ~SSTriangle() override = default;

    void init() override
    {
        auto& r = get_renderer();
        pipeline = r.make_pipeline(PipelineCreateInfo{ .shaders = {
                                                           r.make_shader("triangle/triangle.vert.glsl"),
                                                           r.make_shader("triangle/triangle.frag.glsl"),
                                                       },
                                                       .attachments = { .count = 1, .color_formats = {ImageFormat::R8G8B8A8_SRGB} }, 
                                                      });
    }

    void on_render_graph(RenderGraph& graph) override
    {
        graph.add_graphics_pass(
            "Draw triangle",
            [this](RenderGraph::PassBuilder& pb) {
                auto* w = Engine::get().window;
                pass_out_color.color =
                    pb.create_resource(Image::init("sstriangle output", w->width, w->height, 1,
                                                   ImageFormat::R8G8B8A8_SRGB, ImageUsage::COLOR_ATTACHMENT_BIT));
                pass_out_color.color = pb.access_color(pass_out_color.color);
                get_renderer().imgui_input = *pass_out_color.color;
                return pass_out_color;
            },
            [this](RenderGraph& graph, RenderGraph::PassBuilder& pb) {
                const auto* w = Engine::get().window;
                auto* cmd = pb.open_cmd_buf();
                const VkRenderingAttachmentInfo vkcols[]{ Vks(VkRenderingAttachmentInfo{
                    .imageView = graph.get_acc(pass_out_color.color).image_view.get_md().vk->view,
                    .imageLayout = to_vk(graph.get_acc(pass_out_color.color).layout),
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } } },
                }) };
                const auto vkrinfo = Vks(VkRenderingInfo{
                    .renderArea = { .offset = {},
                                    .extent = { graph.get_img(pass_out_color.color)->width,
                                                graph.get_img(pass_out_color.color)->height } },
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = vkcols,
                });
                VkViewport viewport{ 0.0, 0.0, w->width, w->height, 1.0, 0.0 };
                VkRect2D scissor{ {}, { (uint32_t)w->width, (uint32_t)w->height } };
                cmd->bind_pipeline(pipeline.get());
                cmd->begin_rendering(vkrinfo);
                cmd->set_viewports(&viewport, 1);
                cmd->set_scissors(&scissor, 1);
                cmd->draw(3, 1, 0, 0);
                cmd->end_rendering();
            });

        get_renderer().imgui_renderer->update(&graph, pass_out_color.color);
        pass_out_color.color = Handle<RenderGraph::ResourceAccess>{ get_renderer().imgui_input };

        graph.add_graphics_pass(
            "Copy to swapchain",
            [this](RenderGraph::PassBuilder& pb) {
                auto& r = get_renderer();
                pass_copy_to_swap.swap = pb.import_resource(r.swapchain->get_image());
                pass_copy_to_swap.color = pb.access_resource(pass_out_color.color, ImageLayout::TRANSFER_SRC,
                                                             PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
                pass_copy_to_swap.swap = pb.access_resource(pass_copy_to_swap.swap, ImageLayout::TRANSFER_DST,
                                                            PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT);
                return pass_copy_to_swap;
            },
            [this](RenderGraph& graph, RenderGraph::PassBuilder& pb) {
                auto* cmd = pb.open_cmd_buf();
                cmd->copy(graph.get_img(pass_copy_to_swap.swap).get(), graph.get_img(pass_copy_to_swap.color).get());
            });
        graph.add_graphics_pass(
            "Swapchain to present",
            [this](RenderGraph::PassBuilder& pb) {
                pass_present_swap.swap = pb.access_resource(pass_copy_to_swap.swap, ImageLayout::PRESENT,
                                                            PipelineStage::ALL, PipelineAccess::WRITES);
                return pass_copy_to_swap;
            },
            [](RenderGraph& graph, RenderGraph::PassBuilder& pb) {});
    }

    Handle<Pipeline> pipeline;
    SSTrianglePass pass_out_color;
    SSCopyToSwapPass pass_copy_to_swap;
    SSSwapPresent pass_present_swap;
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
//         auto* w = Engine::get().window;
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
//                 pass.zbuf = pb.import_resource(pf.gbuffer.depth, RenderGraph::Clear::depth_stencil({ 0.0f, 0u }));
//                 pass.zbuf = pb.access_depth_attachment(pass.zbuf);
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
//         VkViewport vkview{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
//         VkRect2D vksciss{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
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
//         auto* w = Engine::get().window;
//         zbuf = info.zbufs;
//         const auto hizpmips = (uint32_t)(std::log2f(std::max(w->width, w->height)) + 1);
//         hiz = g->make_resource(ImageDescriptor{ .name = "hizpyramid",
//                                                 .width = (uint32_t)w->width,
//                                                 .height = (uint32_t)w->height,
//                                                 .mips = (uint32_t)(hizpmips),
//                                                 .format = ImageFormat::R32F,
//                                                 .usage = ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT | ImageUsage::TRANSFER_DST_BIT },
//                                r->frame_count);
//         hiz_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
//             .shaders = { Engine::get().renderer->make_shader("culling/hiz.comp.glsl") }, .layout = r->bindless_pplayout });
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
//         cull_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
//             .shaders = { Engine::get().renderer->make_shader("culling/culling.comp.glsl") },
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
//         light_culling_pipeline = Engine::get().renderer->make_pipeline(PipelineCreateInfo{
//             .shaders = { Engine::get().renderer->make_shader("forwardp/cull_lights.comp.glsl") },
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
//         const auto* w = Engine::get().window;
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