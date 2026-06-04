#include <meshoptimizer/src/meshoptimizer.h>
#include <ranges>
#include <barrier>
#include <execution>
#include <algorithm>
#include "renderer.hpp"
#include <eng/renderer/staging_buffer.hpp>
#include <eng/engine.hpp>
#include <eng/camera.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/renderer/vulkan/to_vk.hpp>
#include <eng/common/to_string.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/ecs/components.hpp>
#include <eng/fs/fs.hpp>
#include <eng/renderer/passes/passes.hpp>
#include <eng/scene.hpp>
#include <eng/math/align.hpp>
#include <assets/shaders/common.hlsli>
#include <reproc++/run.hpp>

namespace eng
{

namespace gfx
{

bool on_window_resize(float x, float y)
{
    auto& r = get_renderer();
    if(r.settings.present_resolution.x != x || r.settings.present_resolution.y != y)
    {
        r.settings.present_resolution = { x, y };
        r.settings.regenerate_swapchain = true;
    }
    r.init_buffered_resources();
    return true;
}

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

void Renderer::BuildGeometryContext::add_descriptor(Handle<Geometry> geometry, const GeometryDescriptor& desc)
{
    auto& batch = batches.emplace_back();
    batch.geom = geometry;
    batch.flags = desc.flags;
    batch.vertex_layout = desc.vertex_layout;
    batch.index_format = desc.index_format;
    batch.indices.insert(batch.indices.end(), desc.indices.begin(), desc.indices.end());
    batch.meshlets.insert(batch.meshlets.end(), desc.meshlets.begin(), desc.meshlets.end());
    batch.geom_ready_signal = desc.signal;

    if(desc.attributes.size())
    {
        batch.positions.insert(batch.positions.end(), desc.vertices.begin(), desc.vertices.end());
        batch.attributes.insert(batch.attributes.end(), desc.attributes.begin(), desc.attributes.end());
    }
    else
    {
        const auto n_vertices = desc.get_num_vertices();
        const auto layout_size = get_vertex_layout_size(desc.vertex_layout);
        const auto n_layout_flts = layout_size / sizeof(float);
        const auto pos_size = get_vertex_layout_size(VertexComponent::POSITION_BIT);
        const auto attrs_size = layout_size - pos_size;
        // const auto att_size_flts = attrs_size / sizeof(float);
        //  this is temp, as i'm too lazy to support nonuniform vertex attribute layouts
        constexpr auto n_required_att_flts = 9;
        const auto n_att_flts = n_required_att_flts;
        std::vector<float> out_pos(n_vertices * 3);
        std::vector<float> out_att(n_vertices * n_att_flts, 0.0f);
        const float* src = static_cast<const float*>(desc.vertices.data());
        for(size_t i = 0; i < n_vertices; ++i)
        {
            const float* vertex_start = src + (i * n_layout_flts);
            out_pos[i * 3 + 0] = vertex_start[0];
            out_pos[i * 3 + 1] = vertex_start[1];
            out_pos[i * 3 + 2] = vertex_start[2];
            const float* att_start = vertex_start + 3;
            float* dst_att_start = &out_att[i * n_att_flts];
            static_assert((int)VertexComponent::ALL == 15);
            for(auto comp : { VertexComponent::NORMAL_BIT, VertexComponent::TANGENT_BIT, VertexComponent::UV0_BIT })
            {
                if(desc.vertex_layout.test(comp))
                {
                    const auto src_flt_offset = get_vertex_component_offset(batch.vertex_layout, comp) / sizeof(float) - 3;
                    const auto flt_offset = get_vertex_component_offset(comp) / sizeof(float) - 3;
                    const auto comp_size = get_vertex_component_size(comp);
                    std::memcpy(dst_att_start + flt_offset, att_start + src_flt_offset, comp_size);
                }
            }
        }
        batch.vertex_layout = VertexComponent::ALL; // todo: remove this when nonuniform layout supported
        batch.positions = std::move(out_pos);
        batch.attributes = std::move(out_att);
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

    get_engine().window->add_on_resize(on_window_resize);

    std::string_view exts[]{ ".hlsl", ".hlsli" };
    new_shaders_listener = get_engine().fs->make_listener("/assets/shaders", exts);
}

void Renderer::init_helper_geom()
{
    // std::vector<Vertex> vertices;
    // std::vector<u32> indices;
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
    // ENG_ASSERT(vertices.size() <= ~u16{});
    // helpergeom.uvsphere = make_geometry(GeometryDescriptor{ .vertices = vertices, .indices = indices });
    // helpergeom.ppskybox = get_renderer().make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_renderer().make_shader("common/skybox.vert.glsl"),
    //                  get_renderer().make_shader("common/skybox.frag.glsl") },
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
    auto linear_sampler_clamp = make_sampler(Sampler::init(ImageFilter::LINEAR, ImageAddressing::CLAMP_EDGE));
    auto nearest_sampler = make_sampler(Sampler::init(ImageFilter::NEAREST, ImageAddressing::REPEAT));
    auto hiz_sampler = make_sampler(Sampler::init(ImageFilter::LINEAR, ImageFilter::LINEAR, ImageAddressing::CLAMP_EDGE,
                                                  ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE,
                                                  SamplerMipmapMode::NEAREST, 0.0f, 1000.0f, 0.0f, SamplerReductionMode::MIN));
    Handle<Sampler> imsamplers[ENG_SAMPLER_COUNT]{};
    imsamplers[ENG_SAMPLER_LINEAR] = linear_sampler;
    imsamplers[ENG_SAMPLER_LINEAR_CLAMP] = linear_sampler_clamp;
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

    // hiz_pipeline = get_renderer().make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_renderer().make_shader("culling/hiz.comp.glsl") }, .layout = common_playout });
    // cull_pipeline = get_renderer().make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_renderer().make_shader("culling/culling.comp.glsl") },
    //     .layout = common_playout,
    // });
    // cullzout_pipeline = get_renderer().make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_renderer().make_shader("common/zoutput.vert.glsl"),
    //                  get_renderer().make_shader("common/zoutput.frag.glsl") },
    //     .layout = common_playout,
    //     .attachments = { .depth_format = ImageFormat::D32_SFLOAT },
    //     .depth_test = true,
    //     .depth_write = true,
    //     .depth_compare = DepthCompare::GREATER,
    //     .culling = CullFace::BACK,
    // });
    // fwdp_cull_lights_pipeline = get_renderer().make_pipeline(PipelineCreateInfo{
    //     .shaders = { get_renderer().make_shader("forwardp/cull_lights.comp.glsl") },
    //     .layout = common_playout,
    // });

    Handle<Pipeline> default_pipelines[(int)MeshPassType::LAST_ENUM]{};
    {
        default_pipelines[(int)MeshPassType::Z_PREPASS] =
            make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/default_z_prepass/z_prepass.vs.hlsl",
                                                     "/assets/shaders/default_z_prepass/z_prepass.ps.hlsl" })
                              .init_image_attachments(PipelineCreateInfo::AttachmentState{
                                  .count = 0, .color_formats = {}, .blend_states = {}, .depth_format = settings.depth_format })
                              .init_depth_test(true, true, settings.rw_depth_compare)
                              .init_topology(Topology::TRIANGLE_LIST, PolygonMode::FILL, CullFace::BACK));

        default_pipelines[(int)MeshPassType::OPAQUE] =
            make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/meshpass/default_unlit.vs.hlsl",
                                                     "/assets/shaders/meshpass/default_unlit.ps.hlsl" })
                              .init_image_attachments(PipelineCreateInfo::AttachmentState{
                                  .count = 8,
                                  .color_formats = { settings.color_format, ImageFormat::UNDEFINED, settings.normal_format },
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

        default_pipelines[(int)MeshPassType::WIREFRAME] =
            make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/default_wireframe/default_wireframe.vs.hlsl",
                                                     "/assets/shaders/default_wireframe/default_wireframe.ps.hlsl" })
                              .init_image_attachments(PipelineCreateInfo::AttachmentState{
                                  .count = 1,
                                  .color_formats = { settings.color_format },
                                  .blend_states = { PipelineCreateInfo::BlendState{ .enable = true,
                                                                                    .src_color_factor = BlendFactor::SRC_ALPHA,
                                                                                    .dst_color_factor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                                                                                    .color_op = BlendOp::ADD,
                                                                                    .src_alpha_factor = BlendFactor::ONE,
                                                                                    .dst_alpha_factor = BlendFactor::ZERO,
                                                                                    .alpha_op = BlendOp::ADD } } })
                              .init_depth_test(false, false, settings.read_depth_compare)
                              .init_topology(Topology::TRIANGLE_LIST, PolygonMode::LINE, CullFace::BACK));
        settings.apply_ao_pipeline = make_pipeline(PipelineCreateInfo::init({ "/assets/shaders/ssao/apply.cs.hlsl" }));
    }

    {
        Handle<ShaderEffect> default_effects[(int)MeshPassType::LAST_ENUM]{};
        for(auto i = 0; i < (int)MeshPassType::LAST_ENUM; ++i)
        {
            default_effects[i] = make_shader_effect(ShaderEffect{ default_pipelines[i] });
        }
        for(auto i = 0; i < (int)MeshPassType::LAST_ENUM; ++i)
        {
            MeshPass pass = MeshPass::init(ENG_FMT("default meshpass {}", to_string((MeshPassType)i)), {});
            pass.effects[i] = default_effects[i];
            if(i == (int)MeshPassType::OPAQUE)
            {
                pass.effects[(int)MeshPassType::Z_PREPASS] = default_effects[(int)MeshPassType::Z_PREPASS];
                // pass.effects[(int)MeshPassType::WIREFRAME] = default_effects[(int)MeshPassType::WIREFRAME];
            }
            settings.default_meshpasses[i] = make_mesh_pass(pass);
        }

        settings.magenta_black_texture =
            make_image("magenta black  texture",
                       Image::init(2, 2, ImageFormat::R8G8B8A8_UNORM, ImageUsage::SAMPLED_BIT, ImageLayout::READ_ONLY));
        settings.white_texture = make_image("white texture", Image::init(2, 2, ImageFormat::R8G8B8A8_UNORM,
                                                                         ImageUsage::SAMPLED_BIT, ImageLayout::READ_ONLY));
        {
            u8 data[] = {
                255, 0, 255, 255, // Pixel 1: Magenta
                0,   0, 0,   255, // Pixel 2: Black
                0,   0, 0,   255, // Pixel 3: Black
                255, 0, 255, 255  // Pixel 4: Magenta
            };
            staging->copy(settings.magenta_black_texture.get(), data, 0, 0);
        }
        {
            u8 data[] = {
                255, 255, 255, 255, // Pixel 1
                255, 255, 255, 255, // Pixel 2
                255, 255, 255, 255, // Pixel 3
                255, 255, 255, 255  // Pixel 4
            };
            staging->copy(settings.white_texture.get(), data, 0, 0);
        }
    }

    {
        // passes.reconstruct_normals = std::make_shared<pass::NormalFromDepth>();
        // passes.ao[(int)AOMode::SSAO] = std::make_shared<pass::SSAO>();
        // passes.ao[(int)AOMode::GTAO] = std::make_shared<pass::GTAO>();
        // passes.ao[(int)AOMode::RTAO] = std::make_shared<pass::RTAO>();
        passes.mesh_passes[(int)MeshPassType::Z_PREPASS] =
            std::make_shared<pass::MeshPass>(MeshPassType::Z_PREPASS, RenderOrder::Z_PREPASS);
        passes.mesh_passes[(int)MeshPassType::OPAQUE] =
            std::make_shared<pass::MeshPass>(MeshPassType::OPAQUE, RenderOrder::MESH_RENDER);
        passes.mesh_passes[(int)MeshPassType::WIREFRAME] =
            std::make_shared<pass::MeshPass>(MeshPassType::WIREFRAME, RenderOrder::MESH_RENDER);
        passes.ao = std::make_shared<pass::SSAO>();
        passes.velocity = std::make_shared<pass::VelocityBuffer>();
    }
}

void Renderer::init_perframes()
{
    static_assert(frame_delay == 2);
    current_data = &frame_datas[0];
    prev_data = &frame_datas[1];
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
    bufs.materials = make_buffer("materials", Buffer::init(1024 * 100, BufferUsage::STORAGE_BIT));
    for(u32 i = 0; i < 2; ++i)
    {
        bufs.transforms[i] = make_buffer(ENG_FMT("trs {}", i), Buffer::init(1024, BufferUsage::STORAGE_BIT));
        bufs.lights[i] = make_buffer(ENG_FMT("lights {}", i), Buffer::init(1024, BufferUsage::STORAGE_BIT));
    }
    {
        const auto* w = get_engine().window;
        const auto num_tiles_x = (u32)std::ceilf(w->width / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles_y = (u32)std::ceilf(w->height / (float)bufs.fwdp_tile_pixels);
        const auto num_tiles = num_tiles_x * num_tiles_y;
        bufs.fwdp_num_tiles = num_tiles;
    }
}

void Renderer::init_buffered_resources()
{
    char resname[128]{};
    size_t resnamelen;
    const auto make_res_name = [&resname, &resnamelen, this](const char* const name, u32 idx) {
        resnamelen = ENG_FMT_TO_N(resname, 127, "{}{}", name, idx);
        return std::string_view{ resname, resnamelen };
    };

    for(auto i = 0u; i < std::size(frame_datas); ++i)
    {
        auto& fd = frame_datas[i];
        auto& rr = fd.render_resources;
        if(!rr.constants)
        {
            rr.constants =
                make_buffer(make_res_name("Constants", i), Buffer::init(sizeof(GPUEngConstants), BufferUsage::STORAGE_BIT));
        }

        for(auto& [name, rrimg, img] :
            { std::make_tuple("Opaque", &rr.opaque,
                              Image::init(settings.render_resolution.x, settings.render_resolution.y, settings.color_format,
                                          ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT)),
              std::make_tuple("Opaque Normals", &rr.normal,
                              Image::init(settings.render_resolution.x, settings.render_resolution.y, settings.normal_format,
                                          ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT)),
              std::make_tuple("Depth", &rr.depth,
                              Image::init(settings.render_resolution.x, settings.render_resolution.y,
                                          settings.depth_format, ImageUsage::DEPTH_BIT | ImageUsage::SAMPLED_BIT)),
              std::make_tuple("Velocity", &rr.velocity,
                              Image::init(settings.render_resolution.x, settings.render_resolution.y,
                                          settings.velocity_format, ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT)) })
        {
            if(*rrimg) { queue_destroy(*rrimg); }
            *rrimg = make_image(make_res_name(name, i), Image{ img });
        }
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
//  if(!get_engine().ecs->has<ecsc::Transform, ecsc::Mesh>(e))
//{
//      ENG_WARN("Entity {} does not have the required components (Transform, Mesh).", *e);
//      return;
//  }
//  auto& mesh = get_engine().ecs->get<ecsc::Mesh>(e);
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
    ENG_TIMER_SCOPED("Renderer update");

    if(settings.override_render_resolution.x != 0 && settings.override_render_resolution.y != 0)
    {
        settings.new_render_resolution = settings.override_render_resolution;
    }
    if(settings.render_resolution != settings.new_render_resolution)
    {
        settings.render_resolution = settings.new_render_resolution;
        get_engine().camera->update_projection(glm::radians(70.0f), 0.1, settings.render_resolution.x,
                                               settings.render_resolution.y);
    }

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

    if(current_frame == 0) { init_buffered_resources(); }

    for(auto* s : current_data->wait_syncs)
    {
        s->wait_cpu(~0ull);
    }
    current_data->wait_syncs.clear();

    current_data->ren_fen->wait_cpu(~0ull);
    current_data->ren_fen->reset();
    current_data->cmdpool->reset();
    current_data->reset_syncs();
    current_data->reset_queries();

    if(current_data->retired_resources.size() > 0)
    {
        std::scoped_lock lock{ current_data->retired_mutex };
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
                buffers.erase(buf->handle);
            }
            else if(auto* img = std::get_if<Handle<Image>>(&rs.resource))
            {
                // ENG_LOG("Removing retired image {} ({})", image_names[*(*img)], *(*img));
                backend->destroy_image(img->get());
                images.erase(img->handle);
            }
        }
        current_data->retired_resources.erase(current_data->retired_resources.begin(), remove_until);
    }

    new_shaders_listener->consume_paths([this](std::vector<fs::Path> paths) {
        if(paths.empty()) { return; }

        std::unordered_set<Handle<Shader>> to_recompile;
        std::unordered_set<Handle<Pipeline>> pps_to_recompile;
        to_recompile.reserve(paths.size());
        for(const auto& p : paths)
        {
            std::vector<Handle<Pipeline>> ppvec;
            auto vec = m_shaders.get_affected_shaders(p, &ppvec);
            to_recompile.insert(vec.begin(), vec.end());
            pps_to_recompile.insert(ppvec.begin(), ppvec.end());
        }

        vkDeviceWaitIdle(RendererBackendVk::get_dev());

        for(auto h : to_recompile)
        {
            auto newsh = Shader::init(h->path);
            backend->make_shader(newsh);
            auto ret = compile_shader(newsh);
            if(!ret)
            {
                backend->destroy_shader(newsh);
                continue;
            }
            backend->destroy_shader(h.get());
            h.get() = std::move(newsh);
        }
        for(auto h : pps_to_recompile)
        {
            backend->destroy_pipeline(h.get());
            compile_pipeline(h);
        }
    });
    if(const auto& qg = get_engine().ecs->get_query_group<ecsc::Mesh>(); mesh_entt_hash != qg.hash)
    {
        ENG_TIMER_SCOPED("Allocate mesh gpu resources");
        for(auto e : qg.entities)
        {
            auto& mesh = get_engine().ecs->get<ecsc::Mesh>(e);
            if(mesh.gpu_resource == ~0u)
            {
                mesh.gpu_resource = *gpu_resource_allocator.allocate();
                new_transforms.push_back(e);
            }
            mesh_renderer.instance_entity(e);
        }
        mesh_entt_hash = qg.hash;
    }
    if(new_shaders.size())
    {
        for(auto h : new_shaders)
        {
            compile_shader(h);
        }
        new_shaders.clear();
    }
    if(new_pipelines.size())
    {
        for(auto h : new_pipelines)
        {
            compile_pipeline(h);
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
                GPUMaterial gpumat{
                    .base_color_idx = descriptor_allocator->get_bindless(DescriptorResource::sampled_image(e->base_color_texture)),
                    .base_color_factor = e->base_color_factor,
                };
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
            const auto& transform = get_engine().ecs->get<ecsc::Transform>(entity);
            const auto& mesh = get_engine().ecs->get<ecsc::Mesh>(entity);
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
        //      auto& l = get_engine().ecs->get<ecsc::Light>(new_lights[i]);
        //      const auto& t = get_engine().ecs->get<ecsc::Transform>(new_lights[i]);
        //      if(l.gpu_index == ~0u) { l.gpu_index = gpu_light_allocator.allocate(); }
        //      GPULight gpul{ t.pos(), l.range, l.color, l.intensity, (u32)l.type };
        //      staging->copy(bufs.lights[0], &gpul, offsetof(GPULightsBuffer, lights_us) + l.gpu_index * sizeof(GPULight),
        //                    sizeof(GPULight));
        //  }
        //  staging->copy(bufs.lights[0], &bufs.light_count, offsetof(GPULightsBuffer, count), 4);
        //  new_lights.clear();
    }

    swapchain->acquire(~0ull, current_data->acq_sem);

    build_pending_geometries();
    build_pending_blases();
    {
        ENG_TIMER_SCOPED("Build passes");
        mesh_renderer.build_passes();
    }

    {
        ENG_TIMER_SCOPED("Compile rendergraph");
        compile_rendergraph();
    }

    {
        ENG_TIMER_SCOPED("render");
        Sync* rg_wait_syncs[]{ staging->flush(true) };
        Sync* rgsync = rgraph->execute(&rg_wait_syncs[0], std::size(rg_wait_syncs));
        gq->wait_sync(current_data->swp_sem).present(swapchain);
    }

    ++current_frame;
    prev_data = current_data;
    current_data = &frame_datas[current_frame % frame_delay];
}

void Renderer::compile_rendergraph()
{
    struct ImportedResources
    {
        RGResourceId constants;
        RGResourceId depth;
        RGResourceId opaque;
        RGResourceId normals;
        RGResourceId velocity;
        RGResourceId final_color;
    };
    const auto import_resources = rgraph->add_graphics_pass<ImportedResources>(
        "SETUP_TARGETS", RenderOrder::SETUP_TARGETS,
        [this](RGBuilder& b, ImportedResources& d) {
            char resname[128]{};
            size_t resnamelen;
            const auto make_res_name = [&resname, &resnamelen, this](const char* const name) {
                resnamelen = ENG_FMT_TO_N(resname, 127, "{}{}", name, current_frame % frame_delay);
                return std::string_view{ resname, resnamelen };
            };

            auto& rr = current_data->render_resources;
            d.constants = b.as_res_id(b.write_buffer(b.import_resource(rr.constants)));
            d.depth = b.as_res_id(b.import_resource(rr.depth, RGClear::depth_stencil(0.0)));
            d.opaque = b.as_res_id(b.import_resource(rr.opaque, RGClear::color()));
            d.normals = b.as_res_id(b.import_resource(rr.normal, RGClear::color()));
            d.velocity = b.as_res_id(b.import_resource(rr.velocity, RGClear::color()));
            d.final_color = b.as_res_id(b.create_resource(
                make_res_name("Final Color"),
                Image::init(settings.render_resolution.x, settings.render_resolution.y, settings.color_format,
                            ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::STORAGE_BIT),
                RGClear::color(), true));
            rr.final_color = d.final_color;
        },
        [this](RGBuilder& b, const ImportedResources& d) {
            auto* c = get_engine().camera;
            GPUEngConstants constants{};

            constants.GPUVertexPositionBufferIndex =
                descriptor_allocator->get_bindless(DescriptorResource::storage_buffer(bufs.positions));
            constants.GPUVertexAttributeBufferIndex =
                descriptor_allocator->get_bindless(DescriptorResource::storage_buffer(bufs.attributes));
            constants.GPUMaterialBufferIndex =
                descriptor_allocator->get_bindless(DescriptorResource::storage_buffer(bufs.materials));
            constants.view = c->get_view();
            constants.proj = c->get_projection();
            constants.proj_view = c->get_projection() * c->get_view();
            constants.inv_view = glm::inverse(c->get_view());
            constants.inv_proj = glm::inverse(c->get_projection());
            constants.inv_proj_view = constants.inv_view * constants.inv_proj;
            constants.cam_pos = c->pos;

            auto* cmd = b.open_cmd_buf();
            get_renderer().staging->copy(b.get_buf(d.constants).buffer.get(), &constants, 0ull, sizeof(constants), false);
            cmd->wait_sync(get_renderer().staging->flush(true));
        });

    pass::PassInitData pass_data{};

    pass_data.depth_buffer = import_resources.depth;
    passes.mesh_passes[(int)MeshPassType::Z_PREPASS]->init(rgraph, pass_data);

    pass_data.gbuffer[(int)pass::GBufferType::VELOCITY] = import_resources.velocity;
    pass_data.gbuffer[(int)pass::GBufferType::DEPTH] = import_resources.depth;
    // pass_data.prev_gbuffer[(int)pass::GBufferType::DEPTH] = prev_data->render_resources.depth;
    // passes.velocity->init(rgraph, pass_data);

    pass_data.color_buffers[(int)pass::GBufferType::DIFFUSE] = import_resources.opaque;
    pass_data.color_buffers[(int)pass::GBufferType::NORMAL] = import_resources.normals;
    passes.mesh_passes[(int)MeshPassType::OPAQUE]->init(rgraph, pass_data);

    // pass_data = {};
    // pass_data.color_buffers[(int)pass::GBufferType::DIFFUSE] = import_resources.opaque;
    // passes.mesh_passes[(int)MeshPassType::WIREFRAME]->init(rgraph, pass_data);

    struct CopyToAccum
    {
        RGAccessId input;
        RGAccessId output;
    };
    rgraph->add_graphics_pass<CopyToAccum>(
        "Copy To Accum", RenderOrder::POST,
        [=, this](RGBuilder& b, CopyToAccum& d) {
            d.input = b.copy_source(import_resources.opaque);
            d.output = b.copy_dest(import_resources.final_color);
        },
        [this](RGBuilder& b, const CopyToAccum& d) {
            auto* cmd = b.open_cmd_buf();
            cmd->copy(b.get_img(d.output).image.get(), b.get_img(d.input).image.get());
        });

    pass_data = {};
    pass_data.gbuffer[(int)pass::GBufferType::DIFFUSE] = import_resources.opaque;
    pass_data.gbuffer[(int)pass::GBufferType::NORMAL] = import_resources.normals;
    pass_data.gbuffer[(int)pass::GBufferType::ACCUMULATION] = import_resources.final_color;
    pass_data.gbuffer[(int)pass::GBufferType::VELOCITY] = import_resources.velocity;
    pass_data.gbuffer[(int)pass::GBufferType::DEPTH] = import_resources.depth;
    // pass_data.prev_gbuffer[(int)pass::GBufferType::DIFFUSE] =  prev_data->render_resources.opaque;
    //  pass_data.prev_gbuffer[(int)pass::GBufferType::NORMAL] = prev_data->render_resources.normal;
    //  pass_data.prev_gbuffer[(int)pass::GBufferType::ACCUMULATION] = prev_data->render_resources.final_color;
    //  pass_data.prev_gbuffer[(int)pass::GBufferType::VELOCITY] = prev_data->render_resources.velocity;
    //  pass_data.prev_gbuffer[(int)pass::GBufferType::DEPTH] = prev_data->render_resources.depth;
    passes.ao->init(rgraph, pass_data);

    const auto imdata = imgui_renderer->update(rgraph);

    struct CopySwapchainData
    {
        RGAccessId input;
        RGAccessId output;
    };
    const auto copyswapchaindata = rgraph->add_graphics_pass<CopySwapchainData>(
        "Copy To Swap", RenderOrder::PRESENT,
        [=, this](RGBuilder& b, CopySwapchainData& data) {
            data.input = b.copy_source(get_renderer().current_data->render_resources.final_color);
            data.output = b.import_resource(swapchain->get_image());
            data.output = b.copy_dest(data.output);
        },
        [this](RGBuilder& pb, const CopySwapchainData& data) {
            auto* cmd = pb.open_cmd_buf();
            auto& dsti = pb.graph->get_img(data.output).get();
            const auto& srci = pb.graph->get_img(data.input).get();
            cmd->wait_sync(current_data->acq_sem, PipelineStage::TRANSFER_BIT);
            cmd->blit(dsti, srci,
                      ImageBlit{ .srclayers = ImageLayers{ 0, { 0, 1 } },
                                 .dstlayers = ImageLayers{ 0, { 0, 1 } },
                                 .srcrange = { {}, { (float)srci.width, (float)srci.height, 1 } },
                                 .dstrange = { {}, { (float)dsti.width, (float)dsti.height, 1 } },
                                 .filter = ImageFilter::LINEAR });
        });
    rgraph->add_graphics_pass<RGAccessId>(
        "Present", RenderOrder::PRESENT,
        [copyswapchaindata](RGBuilder& b, RGAccessId& output) {
            output = b.access_resource(copyswapchaindata.output, ImageLayout::PRESENT, PipelineStage::ALL, PipelineAccess::PRESENT_BIT);
        },
        [this](RGBuilder& b, auto& data) {
            auto* cmd = b.open_cmd_buf();
            cmd->signal_sync(current_data->ren_fen, PipelineStage::ALL);
            cmd->signal_sync(current_data->swp_sem, PipelineStage::ALL);
        });

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
// VkRect2D vksciss{ {}, { (u32)get_engine().window->width, (u32)get_engine().window->height } };
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
//     const auto outputmode = (u32)debug_output;
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
    u32 order = 0;
    float size = (float)buffer.capacity;
    static constexpr const char* units[]{ "B", "KB", "MB", "GB" };
    for(; size >= 1024.0f && order < std::size(units); size /= 1024.0f, ++order) {}
    // ENG_LOG("Creating buffer {} [{:.2f} {}]", name, size, units[order]);
    backend->allocate_buffer(buffer, allocate);
    backend->set_debug_name(buffer, name);
    auto it = buffers.emplace(std::move(buffer));
    if(!it)
    {
        ENG_WARN("Could not create buffer {}", name);
        return Handle<Buffer>{};
    }
    return Handle<Buffer>{ *it };
}

void Renderer::queue_destroy(Handle<Buffer> buffer)
{
    ENG_ASSERT(buffer);
    std::scoped_lock lock{ current_data->retired_mutex };
    current_data->retired_resources.push_back(FrameData::RetiredResource{ buffer, current_frame });
}

Handle<Image> Renderer::make_image(std::string_view name, Image&& image, AllocateMemory allocate, void* user_data)
{
    backend->allocate_image(image, allocate, user_data);
    backend->set_debug_name(image, name);
    auto it = images.emplace(std::move(image));
    if(!it)
    {
        ENG_WARN("Could not create image {}", name);
        return Handle<Image>{};
    }
    return Handle<Image>{ *it };
}

void Renderer::queue_destroy(Handle<Image>& image, bool destroy_now)
{
    ENG_ASSERT(image);
    if(destroy_now)
    {
        backend->destroy_image(image.get());
        images.erase(*image);
    }
    else
    {
        std::scoped_lock lock{ current_data->retired_mutex };
        current_data->retired_resources.push_back(FrameData::RetiredResource{ image, current_frame });
    }
}

Handle<Sampler> Renderer::make_sampler(Sampler&& sampler)
{
    const auto found_handle = samplers.find(sampler);
    if(found_handle) { return found_handle; }
    backend->allocate_sampler(sampler);
    auto ret = samplers.insert(std::move(sampler));
    return ret.handle;
}

Handle<Shader> Renderer::make_shader(const fs::Path& path, Compilation compilation)
{
    const auto h = m_shaders.make_shader(path);
    ENG_ASSERT(h && h->stage != ShaderStage::NONE);
    if(compilation == Compilation::NOW) { compile_shader(h); }
    else { new_shaders.push_back(h); }
    return h;
}

bool Renderer::compile_shader(Handle<Shader> shader)
{
    if(!shader || shader->path.empty() || shader->md.ptr)
    {
        ENG_WARN("Shader invalid, or path empty, or already has metadata allocated");
        return false;
    }
    auto& sh = shader.get();
    backend->make_shader(sh);
    return compile_shader(sh);
}

bool Renderer::compile_shader(Shader& shader)
{
    const auto shhash = m_shaders.get_hash(shader.path);
    fs::FilePtr shfile = get_engine().fs->get_asset(shader.path, fs::OpenMode::READ_BYTES);
    const auto pcpath = fs::Path{ shader.path.string() + ".precompiled" };
    fs::FilePtr pcfile = get_engine().fs->get_asset(pcpath, fs::OpenMode::READ_BYTES);
    if(!shfile && !pcfile)
    {
        ENG_WARN("Shader {} has no source file nor precompiled binary", shader.path.string());
        return false;
    }

    const auto compile_from_bytecode = [this, shhash, &shader, &pcfile]() -> bool {
        u64 readhash{};
        auto readb = pcfile->read((std::byte*)&readhash, 8, 0);
        ENG_ASSERT(readb == 8);
        if(shhash == readhash)
        {
            std::vector<u32> pcdata((pcfile->size() - 8) / 4);
            readb = pcfile->read((std::byte*)pcdata.data(), pcdata.size() * 4, 8);
            ENG_ASSERT(readb % 4 == 0);
            auto res = backend->compile_shader(shader, std::as_bytes(std::span{ pcdata }));
            ENG_ASSERT(res);
            return true;
        }
        return false;
    };

    if(pcfile && pcfile->size() > 8)
    {
        if(compile_from_bytecode())
        {
            ENG_LOG("Compiling shader {} from bytecode", pcpath.string());
            return true;
        }
    }

    ENG_LOG("Compiling shader {} from source", shader.path.string());
    reproc::options opts;
    opts.redirect.parent = true;
    opts.working_directory = "./";

    const char* shader_target = [&shader] {
        // clang-format off
			switch(shader.stage) 
			{
                case ShaderStage::VERTEX_BIT:  { return "vs_6_7"; }
                case ShaderStage::PIXEL_BIT:   { return "ps_6_7"; }
                case ShaderStage::COMPUTE_BIT: { return "cs_6_7"; }
                default: { ENG_WARN("Unhandled case"); return (const char*)nullptr; }
			}
        // clang-format on
    }();
    if(!shader_target) { return false; }

    // compilation should happen from where the folder assets/ is
    const auto include_path = get_engine().fs->make_rel_path("/").string();
    const auto hlsl_path = get_engine().fs->make_rel_path(shader.path).string();
    const auto pcpath_str = get_engine().fs->make_rel_path(pcpath).string();
    const char* args[]{
        "dxc.exe",
        "-T",
        shader_target,
        "-E",
        "main",
        "-spirv",
        "-fvk-use-scalar-layout",
#ifdef ENG_DEBUG_BUILD
        "-Od",
        "-Zi",
#endif
        "-I",
        include_path.c_str(),
        "-Fo",
        pcpath_str.c_str(),
        hlsl_path.c_str(),
        (const char*)nullptr,
    };
    get_engine().fs->delete_file(pcpath_str);
    auto [ret, code] = reproc::run(args, opts);
    if(ret != 0) { return false; }

    pcfile = get_engine().fs->get_asset(pcpath, fs::OpenMode::READ_BYTES);
    if(!pcfile)
    {
        ENG_WARN("Could not open file with compiled shader code {}", pcpath.string());
        return false;
    }

    std::vector<std::byte> pcdata(pcfile->size());
    pcfile->read(pcdata.data(), pcdata.size(), 0);

    pcfile = get_engine().fs->get_asset(pcpath, fs::OpenMode::WRITE_CREATE_BYTES);
    pcfile->write((const std::byte*)&shhash, 8, 0);
    pcfile->write(pcdata.data(), pcdata.size(), 8, true);

    pcfile = get_engine().fs->get_asset(pcpath, fs::OpenMode::READ_BYTES);
    compile_from_bytecode();
    return true;
}

Handle<DescriptorLayout> Renderer::make_layout(const DescriptorLayout& info)
{
    DescriptorLayout layout = info;
    backend->compile_layout(layout);
    dlayouts.push_back(std::move(layout));
    return Handle<DescriptorLayout>{ (u32)dlayouts.size() - 1 };
}

Handle<PipelineLayout> Renderer::make_layout(const PipelineLayout& info)
{
    PipelineLayout layout = info;
    backend->compile_layout(layout);
    pplayouts.push_back(std::move(layout));
    return Handle<PipelineLayout>{ (u32)pplayouts.size() - 1 };
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
        auto handle = Handle<Pipeline>{ (Handle<Pipeline>::StorageType)pipelines.size() };
        pipelines.push_back(std::move(p));
        if(compilation == Compilation::DEFERRED) { new_pipelines.push_back(handle); }
        else { compile_pipeline(handle); }
        return handle;
    }
    return Handle<Pipeline>{ (Handle<Pipeline>::StorageType)std::distance(pipelines.begin(), it) };
}

void Renderer::compile_pipeline(Handle<Pipeline> pipeline)
{
    if(!pipeline || pipeline->md.ptr)
    {
        ENG_WARN("Could not compile pipeline. Handle is null or it already has metadata allocated");
        return;
    }
    auto& pp = pipeline.get();
    backend->make_pipeline(pp);
    for(auto sh : pp.info.shaders)
    {
        if(!sh->md.ptr)
        {
            compile_shader(sh);
            auto it = std::ranges::find(new_shaders, sh);
            if(it != new_shaders.end()) { new_shaders.erase(it); }
        }
        m_shaders.associate_pipeline(sh, pipeline);
    }
    backend->compile_pipeline(pp);
}

void Renderer::destroy_pipeline(Handle<Pipeline> pipeline)
{
    ENG_TODO("TODO: ADD TO DELETEION QUEUE");
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
    if(!mat.mesh_pass) { mat.mesh_pass = settings.default_meshpasses[(int)MeshPassType::OPAQUE]; }
    if(!mat.base_color_texture) { mat.base_color_texture = ImageView::init(settings.white_texture); }
    materials.push_back(std::move(mat));
    const auto handle = Handle<Material>{ (u32)materials.size() - 1 };
    new_materials.push_back(handle);
    return handle;
}

Handle<Geometry> Renderer::make_geometry(const GeometryDescriptor& batch)
{
    const auto ret_handle = Handle<Geometry>{ (u32)geometries.size() };
    new_geometries.add_descriptor(ret_handle, batch);
    geometries.emplace_back();
    return ret_handle;
}

void Renderer::build_pending_geometries()
{
    if(new_geometries.batches.empty()) { return; }

    ENG_TIMER_SCOPED("Build pending geometries");

    struct JobResult
    {
        Handle<Geometry> geometry;
        std::vector<float> positions;
        std::vector<float> attributes;
        std::vector<u16> indices;
        std::vector<Meshlet> meshlets;
    };
    std::vector<JobResult> results(new_geometries.batches.size());
    {
        const auto thread_count = std::thread::hardware_concurrency();
        std::atomic<u64> batch_idx = 0;
        std::vector<std::jthread> jobs(thread_count);
        for(auto& j : jobs)
        {
            j = std::jthread{ [&batch_idx, &results, this]() {
                for(;;)
                {
                    const auto bidx = batch_idx.fetch_add(1);
                    if(bidx >= new_geometries.batches.size()) { return; }
                    auto& batch = new_geometries.batches[bidx];

                    auto& res = results[bidx];
                    res.geometry = batch.geom;
                    if(batch.meshlets.empty())
                    {
                        meshletize_geometry(batch, res.positions, res.attributes, res.indices, res.meshlets);
                        if(batch.geom_ready_signal)
                        {
                            batch.geom_ready_signal->set_value(ParsedGeometryData{ .vertex_layout = batch.vertex_layout,
                                                                                   .positions = res.positions,
                                                                                   .attributes = res.attributes,
                                                                                   .indices = res.indices,
                                                                                   .meshlets = res.meshlets });
                        }
                    }
                    else
                    {
                        ENG_ASSERT(batch.index_format == IndexFormat::U16);
                        res.positions = std::move(batch.positions);
                        res.attributes = std::move(batch.attributes);
                        res.indices.resize(batch.indices.size() / sizeof(u16));
                        memcpy(res.indices.data(), batch.indices.data(), batch.indices.size());
                        res.meshlets = std::move(batch.meshlets);
                    }
                }
            } };
        }
    }

    JobResult final_result{};
    std::vector<glm::vec4> bspheres;
    size_t total_pos{};
    size_t total_att{};
    size_t total_idx{};
    size_t total_mlt{};
    for(const auto& res : results)
    {
        total_pos += res.positions.size();
        total_att += res.attributes.size();
        total_idx += res.indices.size();
        total_mlt += res.meshlets.size();
    }
    final_result.positions.reserve(total_pos);
    final_result.attributes.reserve(total_att);
    final_result.indices.reserve(total_idx);
    bspheres.reserve(total_mlt);
    for(const auto& res : results)
    {
        final_result.positions.insert(final_result.positions.end(), res.positions.begin(), res.positions.end());
        final_result.attributes.insert(final_result.attributes.end(), res.attributes.begin(), res.attributes.end());
        final_result.indices.insert(final_result.indices.end(), res.indices.begin(), res.indices.end());
        for(const auto& mlt : res.meshlets)
        {
            bspheres.push_back(mlt.bounding_sphere);
        }
    }
    resize_buffer(bufs.positions, total_pos * sizeof(float), STAGING_APPEND, true);
    resize_buffer(bufs.attributes, total_att * sizeof(float), STAGING_APPEND, true);
    resize_buffer(bufs.indices, total_idx * sizeof(u16), STAGING_APPEND, true);
    resize_buffer(bufs.bspheres, total_mlt * sizeof(glm::vec4), STAGING_APPEND, true);
    staging->copy(bufs.positions.get(), final_result.positions, STAGING_APPEND);
    staging->copy(bufs.attributes.get(), final_result.attributes, STAGING_APPEND);
    staging->copy(bufs.indices.get(), final_result.indices, STAGING_APPEND);
    staging->copy(bufs.bspheres.get(), bspheres, STAGING_APPEND);
    for(auto& res : results)
    {
        res.geometry->meshlet_range = { .offset = (u32)meshlets.size(), .size = (u32)res.meshlets.size() };
        for(auto& mlt : res.meshlets)
        {
            mlt.vertex_offset += bufs.vertex_count;
            mlt.index_offset += bufs.index_count;
        }
        meshlets.insert(meshlets.end(), res.meshlets.begin(), res.meshlets.end());
        bufs.vertex_count += res.positions.size() / 3;
        bufs.index_count += res.indices.size();
    }

    new_geometries = {};
}

void Renderer::build_pending_blases()
{
    if(new_blases.empty()) { return; }

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

    auto blas_scratch = make_buffer("blas scratch", Buffer::init(total_reqs.build_scratch_size, BufferUsage::AS_SCRATCH));
    queue_destroy(blas_scratch); // destroy later in frame_delay frames

    auto* cmd = current_data->cmdpool->begin();
    cmd->wait_sync(staging->flush(true), PipelineStage::AS_BUILD_BIT);

    total_reqs = {};
    for(auto i = 0u; i < reqs_vec.size(); ++i)
    {
        ASRequirements reqs = reqs_vec[i];
        ASRequirements _reqs;
        backend->make_blas(new_blases[i].get(), _reqs, cmd, &bufs.geom_blas.get(),
                           total_reqs.acceleration_structure_size, &blas_scratch.get(), total_reqs.build_scratch_size);
        total_reqs.acceleration_structure_size += reqs.acceleration_structure_size;
        total_reqs.build_scratch_size += reqs.build_scratch_size;
    }
    cmd->barrier(PipelineStage::AS_BUILD_BIT, PipelineAccess::AS_WRITE_BIT, PipelineStage::AS_BUILD_BIT, PipelineAccess::AS_READ_BIT);

    std::vector<u32> instance_ids;
    std::vector<const Geometry*> tlas_geoms;
    std::vector<glm::mat3x4> trs;
    get_engine().ecs->iterate_components<ecsc::Mesh, ecsc::Transform>([&](ecs::EntityId e, const ecsc::Mesh& m,
                                                                          const ecsc::Transform& t) {
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
        auto tlas_scratch = make_buffer("tlas scratch", Buffer::init(tlas_reqs.build_scratch_size, BufferUsage::AS_SCRATCH));
        queue_destroy(tlas_scratch); // destroy later in frame_delay frames
        bufs.geom_tlas_buffer =
            make_buffer("geom tlas", Buffer::init(tlas_reqs.acceleration_structure_size, BufferUsage::AS_STORAGE));
        auto instances = make_buffer("Tlas blas instances",
                                     Buffer::init(tlas_reqs.instance_data_buffer_size, BufferUsage::AS_BUILD_INPUT));
        queue_destroy(instances);
        bufs.geom_tlas = backend->make_tlas(std::span{ tlas_geoms }, std::span{ std::as_const(trs) },
                                            std::span{ std::as_const(instance_ids) }, tlas_reqs, cmd,
                                            &bufs.geom_tlas_buffer.get(), 0, &tlas_scratch.get(), 0, &instances.get(), 0);
    }

    auto* tlas_done_sync = current_data->get_sync();
    cmd->signal_sync(tlas_done_sync, PipelineStage::AS_BUILD_BIT);
    current_data->cmdpool->end(cmd);
    gq->wait_sync(tlas_done_sync, PipelineStage::RAY_TRACING_BIT);
    gq->with_cmd_buf(cmd);
    gq->submit();
    new_blases.clear();
}

void Renderer::meshletize_geometry(const BuildGeometryBatch& batch, std::vector<float>& out_positions, std::vector<float>& out_attributes,
                                   std::vector<u16>& out_indices, std::vector<Meshlet>& out_meshlets)
{
    auto& context = new_geometries;

    static constexpr auto max_verts = 64u;
    static constexpr auto max_tris = 124u;
    static constexpr auto cone_weight = 0.0f;

    if(batch.indices.empty())
    {
        ENG_WARN("Batch has no indices");
        return;
    }

    // get indices to meshletize
    std::vector<u32> indices(batch.indices.size() / sizeof(u32));
    copy_indices(std::as_writable_bytes(std::span{ indices }), std::span{ batch.indices }, IndexFormat::U32, batch.index_format);

    // get positions to meshletize
    const std::vector<float>& positions = batch.positions;

    const auto max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_verts, max_tris);
    std::vector<meshopt_Meshlet> mlts(max_meshlets);
    std::vector<u32> mlt_vtxs(max_meshlets * max_verts);   // indices to original vertices
    std::vector<u8> mlt_tris(max_meshlets * max_tris * 3); // indices to remapped vertices

    const auto vtx_count = positions.size() / 3;
    const auto pos_stride = 3 * sizeof(float);
    const auto mltcnt = meshopt_buildMeshlets(mlts.data(), mlt_vtxs.data(), mlt_tris.data(), indices.data(), indices.size(),
                                              positions.data(), vtx_count, pos_stride, max_verts, max_tris, cone_weight);

    mlts.resize(mltcnt);
    const auto& last = mlts.back();
    mlt_vtxs.resize(last.vertex_offset + last.vertex_count);
    mlt_tris.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

    // compute bounding spheres
    std::vector<meshopt_Bounds> mlt_bnds(mltcnt);
    for(size_t i = 0; i < mltcnt; ++i)
    {
        auto& m = mlts[i];
        meshopt_optimizeMeshlet(&mlt_vtxs[m.vertex_offset], &mlt_tris[m.triangle_offset], m.triangle_count, m.vertex_count);
        mlt_bnds[i] = meshopt_computeMeshletBounds(&mlt_vtxs[m.vertex_offset], &mlt_tris[m.triangle_offset],
                                                   m.triangle_count, positions.data(), vtx_count, pos_stride);
    }

    // remap and output positions
    out_positions.resize(mlt_vtxs.size() * 3);
    for(size_t i = 0; i < mlt_vtxs.size(); ++i)
    {
        memcpy(&out_positions[i * 3], &positions[mlt_vtxs[i] * 3], 3 * sizeof(float));
    }

    // remap and output attributes
    if(batch.attributes.size() > 0)
    {
        const auto vx_size = get_vertex_layout_size(batch.vertex_layout);
        const auto attr_stride = vx_size - pos_stride;
        const auto att_size_flts = attr_stride / sizeof(float);
        out_attributes.resize(mlt_vtxs.size() * att_size_flts);
        ENG_ASSERT(att_size_flts == 9);
        for(size_t i = 0; i < mlt_vtxs.size(); ++i)
        {
            memcpy(&out_attributes[i * att_size_flts], &batch.attributes[mlt_vtxs[i] * att_size_flts], attr_stride);
        }
    }

    // output meshlet triangles
    out_indices.resize(mlt_tris.size());
    copy_indices(std::as_writable_bytes(std::span{ out_indices }), std::as_bytes(std::span{ mlt_tris }),
                 IndexFormat::U16, IndexFormat::U8);

    // output meshlets
    out_meshlets.resize(mltcnt);
    for(size_t i = 0; i < mltcnt; ++i)
    {
        const auto& m = mlts[i];
        const auto& b = mlt_bnds[i];
        out_meshlets[i] = Meshlet{ .vertex_offset = (i32)m.vertex_offset,
                                   .vertex_count = m.vertex_count,
                                   .index_offset = m.triangle_offset,
                                   .index_count = m.triangle_count * 3,
                                   .bounding_sphere = { b.center[0], b.center[1], b.center[2], b.radius } };
    }
}

Handle<Mesh> Renderer::make_mesh(const MeshDescriptor& batch)
{
    Mesh mesh{ .geometry = batch.geometry, .material = batch.material };
    const auto found_it = std::find(meshes.begin(), meshes.end(), mesh);
    if(found_it != meshes.end()) { return Handle<Mesh>{ (u32)std::distance(meshes.begin(), found_it) }; }
    const u32 idx = meshes.size();
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
    shader_effects.push_back(info);
    return Handle<ShaderEffect>{ (u32)shader_effects.size() - 1 };
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

    auto dsth = make_buffer(backend->get_debug_name(handle.get()), Buffer::init(new_size, handle->usage));
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

SubmitQueue* Renderer::get_queue(QueueType type) { return backend->get_queue(type); }

Shader Shader::init(const fs::Path& path)
{
    const auto stage = [&path] {
        const auto ext = fs::Path{ path }.replace_extension().extension();
        ShaderStage stage{ ShaderStage::NONE };
        if(ext == ".vs") { stage = ShaderStage::VERTEX_BIT; }
        else if(ext == ".ps") { stage = ShaderStage::PIXEL_BIT; }
        else if(ext == ".cs") { stage = ShaderStage::COMPUTE_BIT; }
        else { ENG_WARN("Unrecognized shader extension: {}", ext.string()); }
        return stage;
    }();
    if(stage == ShaderStage::NONE) { return Shader{}; }
    Shader retshader{ path, stage, {} };
    return retshader;
}

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
u32 Swapchain::acquire(u64 timeout, Sync* semaphore, Sync* fence)
{
    current_index = acquire_impl(this, timeout, semaphore, fence);
    return current_index;
}

Handle<Image> Swapchain::get_image() const { return images.at(current_index); }

ImageView Swapchain::get_view() const { return views.at(current_index); }

Handle<Shader> ShaderManager::make_shader(const fs::Path& path)
{
    auto& file = m_files_map[path];
    if(!file.shader)
    {
        auto id = m_shader_alloc.emplace(Shader::init(path));
        if(!id)
        {
            ENG_ASSERT(false, "Failed to create new shader {}", path.string());
            return {};
        }
        auto h = Handle<Shader>{ *id };
        file.path = path;
        file.shader = h;
        file.hash = 0;
        parse_includes(file, nullptr);
    }
    return file.shader;
}

void ShaderManager::parse_includes(File& f, File* pf)
{
    if(f.hash == 0)
    {
        auto file = get_engine().fs->get_asset(f.path, fs::OpenMode::READ_BYTES);
        if(!file)
        {
            ENG_WARN("Couldn't open file {}", f.path.string());
            return;
        }
        f.hash = ENG_HASH(f.hash, file->get_hash());
        const auto shader_dir = f.path.parent_path();
        for(std::string line; file->getline(line);)
        {
            if(!line.starts_with("#include")) { continue; }
            auto start = line.find('"');
            if(start == std::string::npos) { continue; }
            auto end = line.find('"', start + 1);
            if(end == std::string::npos) { continue; }
            fs::Path incpath{ line.begin() + start + 1, line.begin() + end };
            ENG_ASSERT(!incpath.string().starts_with('/'));
            if(!incpath.string().starts_with("assets/")) { incpath = shader_dir / incpath; }
            else { incpath = "/" / incpath; }
            ENG_ASSERT(fs::Path{ incpath }.extension() == ".hlsli");
            auto& inc_file = m_files_map[incpath];
            inc_file.path = incpath;
            parse_includes(inc_file, &f);
            ENG_ASSERT(inc_file.hash != 0);
        }
    }
    if(pf)
    {
        pf->headers_set.insert(&f);
        pf->headers_set.insert(f.headers_set.begin(), f.headers_set.end());
        pf->hash = ENG_HASH(pf->hash, f.hash);
    }
}

std::vector<Handle<Shader>> ShaderManager::get_affected_shaders(const fs::Path& path, std::vector<Handle<Pipeline>>* out_affected_pipelines)
{
    auto chfileit = m_files_map.find(path);
    if(chfileit == m_files_map.end()) { return {}; }
    auto& chfile = chfileit->second;

    std::vector<Handle<Shader>> affected;
    std::set<Handle<Pipeline>> affectedpps;
    for(auto& [path, file] : m_files_map)
    {
        if(&file == &chfile || file.headers_set.contains(&chfile))
        {
            file.hash = 0;
            if(out_affected_pipelines) { affectedpps.insert(file.pipelines_set.begin(), file.pipelines_set.end()); }
            if(file.shader) { affected.push_back(file.shader); }
        }
    }
    if(out_affected_pipelines) { *out_affected_pipelines = { affectedpps.begin(), affectedpps.end() }; }
    return affected;
}

u64 ShaderManager::get_hash(const fs::Path& path)
{
    auto it = m_files_map.find(path);
    if(it == m_files_map.end()) { return 0; }
    if(it->second.hash == 0) { parse_includes(it->second, nullptr); }
    return it->second.hash;
}

void ShaderManager::associate_pipeline(Handle<Shader> sh, Handle<Pipeline> pipeline)
{
    if(!sh) { return; }
    auto it = m_files_map.find(sh->path);
    if(it == m_files_map.end()) { return; }
    it->second.pipelines_set.insert(pipeline);
}

std::vector<Handle<Pipeline>> ShaderManager::get_associated_pipelines(Handle<Shader> sh) const
{
    if(!sh) { return {}; }
    auto it = m_files_map.find(sh->path);
    if(it == m_files_map.end()) { return {}; }
    return std::vector<Handle<Pipeline>>(it->second.pipelines_set.begin(), it->second.pipelines_set.end());
}

float TimestampQuery::to_ms(const TimestampQuery& q)
{
    u64 results[2];
    get_renderer().backend->get_query_pool_results(q.pool, q.index, 2, &results);
    return (float)(((double)(results[1] - results[0])) * get_renderer().backend->limits.timestampPeriodNs * 1e-6);
}

ScopedTimestampQuery::ScopedTimestampQuery(std::string_view label, ICommandBuffer* cmd) : cmd(cmd)
{
    auto& r = get_renderer();
    auto& cd = r.current_data;
    auto& tq = cd->tstamp_queries.emplace_back();
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

void Renderer::FrameData::reset_queries() { available_tstamp_queries = std::move(tstamp_queries); }

Sync* Renderer::FrameData::get_sync()
{
    if(available_syncs.size())
    {
        auto* sync = available_syncs.back();
        available_syncs.pop_back();
        syncs.push_back(sync);
        return sync;
    }
    return syncs.emplace_back(get_renderer().make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE }));
}

void Renderer::FrameData::reset_syncs() { available_syncs = std::move(syncs); }

void Renderer::DebugGeomBuffers::render(CommandBufferVk* cmd, Sync* s)
{
    ENG_ASSERT(s == nullptr);
    if(geometry.empty()) { return; }
    const auto verts = expand_into_vertices();
    ENG_ASSERT(verts.size() > geometry.size() && verts.size() % 2 == 0);
    if(!vpos_buf)
    {
        vpos_buf = get_renderer().make_buffer("debug verts", Buffer::init(verts.size() * sizeof(verts[0]), BufferUsage::STORAGE_BIT));
    }

    ENG_ASSERT(false);
    // get_renderer().sbuf->copy(vpos_buf, verts, 0);
    // get_renderer().sbuf->flush()->wait_cpu(~0ull);

    ENG_ASSERT(false);
    // cmd->bind_resource(1, vpos_buf);
    cmd->draw(verts.size(), 1, 0, 0);
    geometry.clear();
}

std::vector<glm::vec3> Renderer::DebugGeomBuffers::expand_into_vertices()
{
    const auto num_verts = std::transform_reduce(geometry.begin(), geometry.end(), 0ull, std::plus<>{}, [](auto val) {
        // NONE, AABB,
        static constexpr u32 NUM_VERTS[]{ 0u, 24u };
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
                          u32 src_mip, u32 dst_mip, u32 src_layer, u32 dst_layer)
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
