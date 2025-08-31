#include "app.hpp"

#include <eng/engine.hpp>
#include <eng/scene.hpp>

using namespace eng;

namespace app
{

void App::start()
{
    Engine::get().on_init += [this] {
        const auto scene_bunny = Engine::get().scene->load_from_file("occlusion_culling1.glb");
        const auto scene_boxplane = Engine::get().scene->load_from_file("boxplane.glb");
        const auto bunny_instance = Engine::get().scene->instance_entity(scene_bunny);
        //const auto bunny_instance2 = Engine::get().scene->instance_entity(scene_boxplane);
    };
}

void App::update()
{

}

}

    auto& fd = get_frame_data();
const auto frame_num = Engine::get().frame_num;
fd.rendering_fence->wait_cpu(~0ull);
fd.cmdpool->reset();

uint32_t swapchain_index{};
Image* swapchain_image{};
{
    VkResult acquire_ret;
    swapchain_index = swapchain.acquire(&acquire_ret, ~0ull, fd.acquire_semaphore);
    if(acquire_ret != VK_SUCCESS)
    {
        ENG_WARN("Acquire image failed with: {}", static_cast<uint32_t>(acquire_ret));
        return;
    }
    swapchain_image = &swapchain.images[swapchain_index].get();
}

fd.rendering_fence->reset();

static glm::mat4 s_view = Engine::get().camera->prev_view;
if(true || (glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
{
    s_view = Engine::get().camera->prev_view;
}

{
    const float hx = (halton(Engine::get().frame_num % 4u, 2) * 2.0 - 1.0);
    const float hy = (halton(Engine::get().frame_num % 4u, 3) * 2.0 - 1.0);
    const glm::mat3 rand_mat =
        glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

    // const auto ldir = glm::normalize(*(glm::vec3*)Engine::get().scene->debug_dir_light_dir);
    // const auto cam = Engine::get().camera->pos;
    // auto eye = -ldir * 30.0f;
    // const auto lview = glm::lookAt(eye, eye + ldir, glm::vec3{ 0.0f, 1.0f, 0.0f });
    // const auto eyelpos = lview * glm::vec4{ cam, 1.0f };
    // const auto offset = glm::translate(glm::mat4{ 1.0f }, -glm::vec3{ eyelpos.x, eyelpos.y, 0.0f });
    // const auto dir_light_view = offset * lview;
    // const auto eyelpos2 = dir_light_view * glm::vec4{ cam, 1.0f };
    // ENG_LOG("CAMERA EYE DOT {} {}", eyelpos2.x, eyelpos2.y);
    //// const auto dir_light_proj = glm::perspectiveFov(glm::radians(90.0f), 8.0f * 1024.0f, 8.0f * 1024.0f, 0.0f, 150.0f);

    // GPUVsmConstantsBuffer vsmconsts{
    //     .dir_light_view = dir_light_view,
    //     .dir_light_dir = ldir,
    //     .num_pages_xy = VSM_VIRTUAL_PAGE_RESOLUTION,
    //     .max_clipmap_index = 0,
    //     .texel_resolution = float(VSM_PHYSICAL_PAGE_RESOLUTION),
    //     .num_frags = 0,
    // };

    // for(int i = 0; i < VSM_NUM_CLIPMAPS; ++i)
    //{
    //     float halfSize = float(VSM_CLIP0_LENGTH) * 0.5f * std::exp2f(float(i));
    //     float splitNear = (i == 0) ? 0.1f : float(VSM_CLIP0_LENGTH) * std::exp2f(float(i - 1));
    //     float splitFar = float(VSM_CLIP0_LENGTH) * std::exp2f(float(i));
    //     splitNear = 1.0;
    //     splitFar = 75.0;
    //     vsmconsts.dir_light_proj_view[i] =
    //         glm::ortho(-halfSize, +halfSize, -halfSize, +halfSize, splitNear, splitFar) * dir_light_view;
    // }

    GPUConstantsBuffer constants{
        .debug_view = s_view,
        .view = Engine::get().camera->get_view(),
        .proj = Engine::get().camera->get_projection(),
        .proj_view = Engine::get().camera->get_projection() * Engine::get().camera->get_view(),
        .inv_view = glm::inverse(Engine::get().camera->get_view()),
        .inv_proj = glm::inverse(Engine::get().camera->get_projection()),
        .inv_proj_view = glm::inverse(Engine::get().camera->get_projection() * Engine::get().camera->get_view()),
        .cam_pos = Engine::get().camera->pos,
    };
    staging_manager->copy(fd.constants, &constants, 0, { 0, sizeof(constants) });
    // staging_buffer->stage(vsm.constants_buffer, vsmconsts, 0ull);
}

if(flags.test_clear(RenderFlags::DIRTY_TRANSFORMS_BIT)) { build_transforms_buffer(); }

uint32_t old_triangles = *((uint32_t*)geom_main_bufs.buf_draw_cmds->memory + 1);
bake_indirect_commands();
staging_manager->flush();
const auto cmd = fd.cmdpool->begin();
// rendergraph.render(cmd);

struct PushConstantsCulling
{
    uint32_t constants_index;
    uint32_t ids_index;
    uint32_t post_cull_ids_index;
    uint32_t bs_index;
    uint32_t transforms_index;
    uint32_t indirect_commands_index;
    uint32_t hiz_source;
    uint32_t hiz_dest;
    uint32_t hiz_width;
    uint32_t hiz_height;
};

PushConstantsCulling push_constants_culling{ .constants_index = bindless_pool->get_index(fd.constants),
                                             .ids_index = bindless_pool->get_index(geom_main_bufs.buf_draw_ids),
                                             .post_cull_ids_index = bindless_pool->get_index(geom_main_bufs.buf_final_draw_ids),
                                             .bs_index = bindless_pool->get_index(geom_main_bufs.buf_draw_bs),
                                             .transforms_index = bindless_pool->get_index(geom_main_bufs.transform_bufs[0]),
                                             .indirect_commands_index = bindless_pool->get_index(geom_main_bufs.buf_draw_cmds) };

{
    auto& dep_image = fd.gbuffer.depth_buffer_image.get();
    auto& hiz_image = fd.hiz_pyramid.get();
    cmd->bind_pipeline(hiz_pipeline.get());
    bindless_pool->bind(cmd);
    if(true || (glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
    {
        cmd->clear_depth_stencil(hiz_image, 0.0f, 0);
        cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                     PipelineAccess::SHADER_RW);
        cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::ALL,
                     PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::READ_ONLY);

        push_constants_culling.hiz_width = hiz_image.width;
        push_constants_culling.hiz_height = hiz_image.height;

        for(auto i = 0u; i < hiz_image.mips; ++i)
        {
            if(i == 0)
            {
                push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
                    make_view(ImageViewDescriptor{ .image = fd.gbuffer.depth_buffer_image, .aspect = ImageAspect::DEPTH }),
                    hiz_sampler, ImageLayout::READ_ONLY }));
            }
            else
            {
                push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
                    make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { i - 1, 1 } }),
                    hiz_sampler, ImageLayout::GENERAL }));
            }
            push_constants_culling.hiz_dest = bindless_pool->get_index(make_texture(TextureDescriptor{
                make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { i, 1 } }),
                {},
                ImageLayout::GENERAL }));
            push_constants_culling.hiz_width = std::max(hiz_image.width >> i, 1u);
            push_constants_culling.hiz_height = std::max(hiz_image.height >> i, 1u);
            cmd->push_constants(VK_SHADER_STAGE_ALL, &push_constants_culling, { 0, sizeof(push_constants_culling) });
            cmd->dispatch((push_constants_culling.hiz_width + 31) / 32, (push_constants_culling.hiz_height + 31) / 32, 1);
            cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                         PipelineAccess::SHADER_RW);
        }
    }
    else
    {
        cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::ALL,
                     PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::READ_ONLY);
    }
    push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
        make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { 0u, hiz_image.mips } }),
        hiz_sampler, ImageLayout::GENERAL }));
    push_constants_culling.hiz_dest = bindless_pool->get_index(make_texture(TextureDescriptor{
        make_view(ImageViewDescriptor{ .image = fd.hiz_debug_output, .aspect = ImageAspect::COLOR }), {}, ImageLayout::GENERAL }));
    cmd->clear_color(fd.hiz_debug_output.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
    cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                 PipelineAccess::SHADER_RW);
    cmd->bind_pipeline(cull_pipeline.get());
    cmd->push_constants(VK_SHADER_STAGE_ALL, &push_constants_culling, { 0, sizeof(push_constants_culling) });
    cmd->dispatch((meshlet_instances.size() + 63) / 64, 1, 1);
    cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_WRITE_BIT, PipelineStage::INDIRECT_BIT,
                 PipelineAccess::INDIRECT_READ_BIT);
}

VkRenderingAttachmentInfo rainfos[]{
    Vks(VkRenderingAttachmentInfo{ .imageView = VkImageViewMetadata::get(swapchain_image->default_view.get()).view,
                                   .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                   .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                   .clearValue = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } } }),
    Vks(VkRenderingAttachmentInfo{ .imageView =
                                       VkImageViewMetadata::get(fd.gbuffer.depth_buffer_image->default_view.get()).view,
                                   .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                   .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                   .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0 } } })
};
const auto rinfo = Vks(VkRenderingInfo{ .renderArea = { { 0, 0 }, { swapchain_image->width, swapchain_image->height } },
                                        .layerCount = 1,
                                        .colorAttachmentCount = 1,
                                        .pColorAttachments = rainfos,
                                        .pDepthAttachment = &rainfos[1] });
struct push_constants_1
{
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t vertex_attributes_index;
    uint32_t transforms_index;
    uint32_t constants_index;
    uint32_t meshlet_instance_index;
    uint32_t meshlet_ids_index;
    uint32_t meshlet_bs_index;
    uint32_t hiz_pyramid_index;
    uint32_t hiz_debug_index;
};
push_constants_1 pc1{
    .indices_index = bindless_pool->get_index(geom_main_bufs.buf_indices),
    .vertex_positions_index = bindless_pool->get_index(geom_main_bufs.buf_vpos),
    .vertex_attributes_index = bindless_pool->get_index(geom_main_bufs.buf_vattrs),
    .transforms_index = bindless_pool->get_index(geom_main_bufs.transform_bufs[0]),
    .constants_index = bindless_pool->get_index(fd.constants),
    .meshlet_instance_index = bindless_pool->get_index(geom_main_bufs.buf_draw_ids),
    .meshlet_ids_index = bindless_pool->get_index(geom_main_bufs.buf_final_draw_ids),
    .meshlet_bs_index = bindless_pool->get_index(geom_main_bufs.buf_draw_bs),
    .hiz_pyramid_index = push_constants_culling.hiz_source,
    .hiz_debug_index = bindless_pool->get_index(make_texture(TextureDescriptor{
        make_view(ImageViewDescriptor{ .image = fd.hiz_debug_output

        }),
        make_sampler(SamplerDescriptor{ .mip_lod = { 0.0f, 1.0f, 0.0 } }), ImageLayout::READ_ONLY })),
};

cmd->bind_index(geom_main_bufs.buf_indices.get(), 0, VK_INDEX_TYPE_UINT16);
cmd->barrier(*swapchain_image, PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::COLOR_OUT_BIT,
             PipelineAccess::COLOR_WRITE_BIT, ImageLayout::UNDEFINED, ImageLayout::ATTACHMENT);
cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE, PipelineStage::EARLY_Z_BIT,
             PipelineAccess::DS_RW, ImageLayout::UNDEFINED, ImageLayout::ATTACHMENT);
cmd->begin_rendering(rinfo);

VkViewport viewport{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
VkRect2D scissor{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
for(auto i = 0u, off = 0u; i < multibatches.size(); ++i)
{
    const auto& mb = multibatches.at(i);
    const auto& p = pipelines.at(mb.pipeline);
    cmd->bind_pipeline(p);
    if(i == 0) { bindless_pool->bind(cmd); }
    cmd->push_constants(VK_SHADER_STAGE_ALL, &pc1, { 0u, sizeof(pc1) });
    cmd->set_viewports(&viewport, 1);
    cmd->set_scissors(&scissor, 1);
    cmd->draw_indexed_indirect_count(geom_main_bufs.buf_draw_cmds.get(), 8, geom_main_bufs.buf_draw_cmds.get(), 0,
                                     geom_main_bufs.command_count, sizeof(DrawIndirectCommand));
}
cmd->end_rendering();

Engine::get().imgui_renderer->render(cmd);

cmd->barrier(*swapchain_image, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, PipelineStage::ALL,
             PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::PRESENT);

fd.cmdpool->end(cmd);
submit_queue->with_cmd_buf(cmd)
    .wait_sync(staging_manager->flush(), PipelineStage::ALL)
    .wait_sync(fd.acquire_semaphore, PipelineStage::COLOR_OUT_BIT)
    .signal_sync(fd.rendering_semaphore, PipelineStage::ALL)
    .signal_sync(fd.rendering_fence)
    .submit();

submit_queue->wait_sync(fd.rendering_semaphore, PipelineStage::ALL).present(&swapchain);
if(!flags.empty()) { ENG_WARN("render flags not empty at the end of the frame: {:b}", flags.flags); }

flags.clear();
submit_queue->wait_idle();

uint32_t new_triangles = *((uint32_t*)geom_main_bufs.buf_draw_cmds->memory + 2);
ENG_LOG("NUM TRIANGLES (PRE | POST) {} | {}; DIFF: {}", old_triangles, new_triangles, new_triangles - old_triangles);