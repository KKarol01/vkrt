#pragma once

#include <cstdint>
#include <span>
#include <compare>
#include <memory>
#include <utility>
#include <array>
#include <filesystem>
#include <unordered_set>
#include <glm/glm.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/handlesparsevec.hpp>
#include <eng/common/handleflatset.hpp>
#include <eng/common/callback.hpp>
#include <eng/common/slotallocator.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{
namespace gfx
{
class ImGuiRenderer;
struct Sync;
struct SyncCreateInfo;
class BindlessPool;
class CommandBuffer;
class CommandPool;
class StagingBuffer;
class SubmitQueue;
struct VkImageMetadata;
struct VkImageViewMetadata;
struct VkShaderMetadata;
struct VkPipelineMetadata;
class RenderGraph;
struct RenderGraphPasses;

struct ImageBlockData
{
    uint32_t size;
    uint32_t x, y, z;
};

ImageBlockData get_block_data(ImageFormat format);

inline size_t get_vertex_component_size(VertexComponent a)
{
    switch(a)
    {
    case VertexComponent::NONE:
        return 0;
    case VertexComponent::POSITION_BIT:
        return 3 * sizeof(float);
    case VertexComponent::NORMAL_BIT:
        return 3 * sizeof(float);
    case VertexComponent::TANGENT_BIT:
        return 4 * sizeof(float);
    case VertexComponent::UV0_BIT:
        return 2 * sizeof(float);
    }
    ENG_ERROR("Undefined case");
    return 0;
}

inline size_t get_vertex_layout_size(Flags<VertexComponent> a)
{
    static constexpr VertexComponent comps[]{ VertexComponent::POSITION_BIT, VertexComponent::NORMAL_BIT,
                                              VertexComponent::TANGENT_BIT, VertexComponent::UV0_BIT };
    size_t sum = 0;
    for(auto e : std::span{ comps })
    {
        if(a.test_clear(e)) { sum += get_vertex_component_size(e); }
    }
    assert(a.empty());
    return sum;
}

inline size_t get_vertex_component_offset(Flags<VertexComponent> layout, VertexComponent comp)
{
    return get_vertex_layout_size(layout & (std::to_underlying(comp) - 1)); // calc size of all previous present components in the bitmask
}

inline size_t get_index_size(IndexFormat a)
{
    switch(a)
    {
    case IndexFormat::U8:
        return 1;
    case IndexFormat::U16:
        return 2;
    case IndexFormat::U32:
        return 4;
    }
    ENG_ERROR("Undefined case");
    return 0;
}

struct Shader
{
    auto operator==(const Shader& o) const { return path == o.path; }
    std::filesystem::path path;
    ShaderStage stage{ ShaderStage::NONE };
    union Metadata {
        VkShaderMetadata* vk{};
    } md;
};

// todo: make sure when the user passes it, it gets checked for compatibility (i think it does -- see pplayouts)
struct PipelineLayoutCreateInfo
{
    inline static constexpr auto MAX_PUSH_BYTES = 128u;
    struct SetLayout
    {
        struct Binding
        {
            auto operator<=>(const Binding& a) const = default;

            PipelineBindingType type{};
            uint32_t slot{};
            uint32_t size{};
            Flags<ShaderStage> stages{};
            Flags<PipelineBindingFlags> flags{};
            const Handle<Sampler>* immutable_samplers{};
        };

        auto operator<=>(const SetLayout& a) const = default;

        Flags<PipelineSetFlags> flags{};
        std::vector<Binding> bindings;
    };
    struct PushRange
    {
        auto operator<=>(const PushRange& a) const = default;
        Flags<ShaderStage> stages{};
        uint32_t size{};
    };

    auto operator<=>(const PipelineLayoutCreateInfo& a) const = default;

    std::vector<SetLayout> sets{};
    PushRange range{};
};

struct DescriptorPoolCreateInfo
{
    struct Pool
    {
        PipelineBindingType type;
        uint32_t count{};
    };
    Flags<DescriptorPoolFlags> flags{};
    uint32_t max_sets{};
    std::vector<Pool> pools;
};

struct DescriptorPool
{
    Handle<DescriptorSet> allocate(Handle<PipelineLayout> playout, uint32_t dset_idx);
    DescriptorSet& get_dset(Handle<DescriptorSet> set);
    DescriptorPoolCreateInfo info{};
    std::vector<DescriptorSet> sets;
    void* metadata{};
};

struct DescriptorSet
{
    void* metadata{};
};

struct PipelineLayout
{
    bool operator==(const PipelineLayout& a) const { return info == a.info; }
    bool is_compatible(const PipelineLayout& a) const;
    PipelineLayoutCreateInfo info;
    void* metadata{};
};

struct PipelineCreateInfo
{
    struct VertexBinding
    {
        auto operator<=>(const VertexBinding&) const = default;
        uint32_t binding;
        uint32_t stride;
        bool instanced{ false };
    };

    struct VertexAttribute
    {
        auto operator<=>(const VertexAttribute&) const = default;
        uint32_t location;
        uint32_t binding;
        VertexFormat format{};
        uint32_t offset;
    };

    struct StencilState
    {
        auto operator<=>(const StencilState&) const = default;
        StencilOp fail;
        StencilOp pass;
        StencilOp depth_fail;
        CompareOp compare;
        uint32_t compare_mask{ ~0u };
        uint32_t write_mask{ ~0u };
        uint32_t ref{};
    };

    struct BlendState
    {
        auto operator<=>(const BlendState&) const = default;
        bool enable{ false };
        BlendFactor src_color_factor{};
        BlendFactor dst_color_factor{};
        BlendOp color_op{};
        BlendFactor src_alpha_factor{};
        BlendFactor dst_alpha_factor{};
        BlendOp alpha_op{};
        uint32_t r : 1 { 1 };
        uint32_t g : 1 { 1 };
        uint32_t b : 1 { 1 };
        uint32_t a : 1 { 1 };
    };

    struct AttachmentState
    {
        auto operator==(const AttachmentState& o) const
        {
            if(count != o.count) { return false; }
            if(depth_format != o.depth_format) { return false; }
            for(auto i = 0u; i < count; ++i)
            {
                if(color_formats.at(i) != o.color_formats.at(i) || blend_states.at(i) != o.blend_states.at(i))
                {
                    return false;
                }
            }
            return true;
        }

        uint32_t count{};
        std::array<ImageFormat, 8> color_formats{};
        std::array<BlendState, 8> blend_states{};
        ImageFormat depth_format{};
        ImageFormat stencil_format{};
    };

    bool operator==(const PipelineCreateInfo& a) const = default;

    std::vector<Handle<Shader>> shaders;
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;
    Handle<PipelineLayout> layout;

    AttachmentState attachments;
    bool depth_test{ false };
    bool depth_write{ false };
    DepthCompare depth_compare{ DepthCompare::NEVER };
    bool stencil_test{ false };
    StencilState stencil_front;
    StencilState stencil_back;

    PolygonMode polygon_mode{ PolygonMode::FILL };
    CullFace culling{ CullFace::NONE };
    bool front_is_ccw{ true };
    float line_width{ 1.0f };
};

struct Pipeline
{
    bool operator==(const Pipeline& a) const { return info == a.info; }
    PipelineCreateInfo info;
    PipelineType type{};
    union Metadata {
        VkPipelineMetadata* vk{};
    } md;
};

struct Geometry
{
    auto operator<=>(const Geometry& a) const = default;
    Range32u meshlet_range{}; // position inside meshlet buffer
    // VkAccelerationStructureKHR blas{};
    // Handle<Buffer> blas_buffer{};
};

struct ShaderEffect
{
    auto operator<=>(const ShaderEffect&) const = default;
    Handle<Pipeline> pipeline;
};

struct MeshPassCreateInfo
{
    std::string name;
    std::array<Handle<ShaderEffect>, (uint32_t)RenderPassType::LAST_ENUM> effects;
};

struct MeshPass
{
    bool operator==(const MeshPass& o) const { return name == o.name; }
    std::string name;
    std::array<Handle<ShaderEffect>, (uint32_t)RenderPassType::LAST_ENUM> effects;
};

struct Material
{
    auto operator<=>(const Material& t) const = default;
    Handle<MeshPass> mesh_pass;
    Handle<Texture> base_color_texture;
};

struct Mesh
{
    auto operator<=>(const Mesh& t) const = default;
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct Meshlet
{
    int32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t index_offset;
    uint32_t index_count;
    glm::vec4 bounding_sphere{};
};

struct GeometryDescriptor
{
    Flags<GeometryFlags> flags;
    Flags<VertexComponent> vertex_layout;
    IndexFormat index_format;
    std::span<const float> vertices;
    std::span<const std::byte> indices;
};

struct BufferDescriptor
{
    std::string name;
    size_t size{};
    Flags<BufferUsage> usage{};
};

struct Buffer
{
    constexpr Buffer() noexcept = default;
    explicit Buffer(const BufferDescriptor& info) noexcept : name(info.name), usage(info.usage), capacity(info.size) {}

    std::string name;
    Flags<BufferUsage> usage{};
    size_t capacity{};
    size_t size{};
    void* metadata{};
    void* memory{};
};

struct ImageDescriptor
{
    std::string name;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1 };
    uint32_t mips{ 1 };
    ImageFormat format{ ImageFormat::R8G8B8A8_UNORM };
    ImageType type{ ImageType::TYPE_2D };
    Flags<ImageUsage> usage{};
    std::span<const std::byte> data;
};

struct Image
{
    Image() noexcept = default;
    explicit Image(const ImageDescriptor& info) noexcept
        : name(info.name), width(info.width), height(info.height), depth(info.depth), mips(info.mips),
          format(info.format), type(info.type), usage(info.usage)
    {
    }

    ImageViewType deduce_view_type() const
    {
        return type == ImageType::TYPE_1D   ? ImageViewType::TYPE_1D
               : type == ImageType::TYPE_2D ? ImageViewType::TYPE_2D
               : type == ImageType::TYPE_3D ? ImageViewType::TYPE_3D
                                            : ImageViewType::NONE;
    }

    Flags<ImageAspect> deduce_aspect() const
    {
        Flags<ImageAspect> f;
        if(usage.test(ImageUsage::DEPTH_BIT)) { f |= ImageAspect::DEPTH; }
        if(usage.test(ImageUsage::STENCIL_BIT)) { f |= ImageAspect::STENCIL; }
        if(!f) { f |= ImageAspect::COLOR; }
        return f;
    }

    std::string name;
    ImageType type{ ImageType::TYPE_2D };
    ImageFormat format{};
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1u };
    uint32_t mips{ 1u };
    uint32_t layers{ 1u };
    Flags<ImageUsage> usage{ ImageUsage::NONE };
    ImageLayout current_layout{ ImageLayout::UNDEFINED };
    Handle<ImageView> default_view;
    union Metadata {
        VkImageMetadata* vk{};
    } md;
};

struct ImageViewDescriptor
{
    std::string name;
    Handle<Image> image;
    std::optional<ImageViewType> view_type;
    std::optional<ImageFormat> format;
    std::optional<Flags<ImageAspect>> aspect;
    Range32u mips{ 0, ~0u };
    Range32u layers{ 0, ~0u };
    // swizzle always identity for now
};

struct ImageView
{
    bool operator==(const ImageView& a) const
    {
        return image == a.image && type == a.type && format == a.format && aspect == a.aspect && mips == a.mips &&
               layers == a.layers;
    }
    std::string name;
    Handle<Image> image;
    ImageViewType type{};
    ImageFormat format{};
    Flags<ImageAspect> aspect{};
    Range32u mips{};
    Range32u layers{};
    union Metadata {
        VkImageViewMetadata* vk{};
    } md;
};

struct ImageSubRange
{
    Range32u mips{};
    Range32u layers{};
};

struct ImageSubLayers
{
    uint32_t mip{};
    Range32u layers{};
};

struct ImageBlit
{
    ImageSubLayers srclayers{};
    ImageSubLayers dstlayers{};
    Range3D32i srcrange{};
    Range3D32i dstrange{};
};

struct ImageCopy
{
    ImageSubLayers srclayers{};
    ImageSubLayers dstlayers{};
    Vec3i32 srcoffset{};
    Vec3i32 dstoffset{};
    Vec3u32 extent{};
};

struct SamplerDescriptor
{
    auto operator<=>(const SamplerDescriptor& a) const = default;
    std::array<ImageFilter, 2> filtering{ ImageFilter::LINEAR, ImageFilter::LINEAR }; // [min, mag]
    std::array<ImageAddressing, 3> addressing{ ImageAddressing::REPEAT, ImageAddressing::REPEAT, ImageAddressing::REPEAT }; // u, v, w
    std::array<float, 3> mip_lod{ 0.0f, 1000.0f, 0.0f }; // min, max, bias
    SamplerMipmapMode mipmap_mode{ SamplerMipmapMode::LINEAR };
    std::optional<SamplerReductionMode> reduction_mode{};
};

struct Sampler
{
    auto operator==(const Sampler& a) const { return info == a.info; }
    SamplerDescriptor info;
    void* metadata{};
};

struct TextureDescriptor
{
    Handle<ImageView> view;
    ImageLayout layout;
    bool is_storage{ false };
};

struct Texture
{
    auto operator<=>(const Texture& t) const = default;
    Handle<ImageView> view;
    ImageLayout layout{ ImageLayout::READ_ONLY };
    bool is_storage{ false };
};

struct MaterialDescriptor
{
    std::string mesh_pass{ "default_unlit" };
    Handle<Texture> base_color_texture;
    Handle<Texture> normal_texture;
    Handle<Texture> metallic_roughness_texture;
};

struct MeshDescriptor
{
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct InstanceSettings
{
    ecs::entity entity;
};

struct BLASInstanceSettings
{
    ecs::entity entity;
};

struct VsmData
{
    Handle<Buffer> constants_buffer;
    Handle<Buffer> free_allocs_buffer;
    Handle<Image> shadow_map_0;
    // VkImageView view_shadow_map_0_general{};
    Handle<Image> dir_light_page_table;
    // VkImageView view_dir_light_page_table_general{};
    Handle<Image> dir_light_page_table_rgb8;
    // VkImageView view_dir_light_page_table_rgb8_general{};
};

struct Swapchain
{
    using acquire_impl_fptr = uint32_t (*)(Swapchain* a, uint64_t timeout, Sync* semaphore, Sync* fence);
    static inline acquire_impl_fptr acquire_impl{};
    uint32_t acquire(uint64_t timeout = -1ull, Sync* semaphore = nullptr, Sync* fence = nullptr);
    Handle<Image> get_image() const;
    Handle<ImageView> get_view() const;
    void* metadata{};
    std::vector<Handle<Image>> images;
    std::vector<Handle<ImageView>> views;
    uint32_t current_index{ 0ul };
};

class RendererBackend
{
  public:
    virtual ~RendererBackend() = default;

    virtual void init() = 0;

    virtual Buffer make_buffer(const BufferDescriptor& info) = 0;
    virtual void destroy_buffer(Buffer& b) = 0;
    virtual Image make_image(const ImageDescriptor& info) = 0;
    virtual void make_view(ImageView& view) = 0;
    virtual Sampler make_sampler(const SamplerDescriptor& info) = 0;
    virtual void make_shader(Shader& shader) = 0;
    virtual bool compile_shader(const Shader& shader) = 0;
    virtual bool compile_pplayout(PipelineLayout& layout) = 0;
    virtual void make_pipeline(Pipeline& pipeline) = 0;
    virtual bool compile_pipeline(const Pipeline& pipeline) = 0;
    virtual Sync* make_sync(const SyncCreateInfo& info) = 0;
    virtual void destory_sync(Sync*) = 0;
    virtual Swapchain* make_swapchain() = 0;
    virtual SubmitQueue* get_queue(QueueType type) = 0;
    virtual DescriptorPool make_descpool(const DescriptorPoolCreateInfo& info) = 0;
    virtual DescriptorSet allocate_set(DescriptorPool& pool, const PipelineLayout& playout, uint32_t dset_idx) = 0;
};

} // namespace gfx
} // namespace eng

ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::VertexBinding, eng::hash::combine_fnv1a(t.binding, t.stride, t.instanced));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::VertexAttribute,
                    eng::hash::combine_fnv1a(t.location, t.binding, t.format, t.offset));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::StencilState,
                    eng::hash::combine_fnv1a(t.fail, t.pass, t.depth_fail, t.compare, t.compare_mask, t.write_mask, t.ref));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::BlendState,
                    eng::hash::combine_fnv1a(t.enable, t.src_color_factor, t.dst_color_factor, t.color_op,
                                             t.src_alpha_factor, t.dst_alpha_factor, t.alpha_op, t.r, t.g, t.b, t.a));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::AttachmentState, [&t] {
    uint64_t hash = 0;
    // clang-format off
    for(auto i=0u; i<t.count; ++i) { hash = eng::hash::combine_fnv1a(hash, t.color_formats.at(i)); }
    for(auto i=0u; i<t.count; ++i) { hash = eng::hash::combine_fnv1a(hash, t.blend_states.at(i)); }
    // clang-format on
    hash = eng::hash::combine_fnv1a(hash, t.count, t.depth_format, t.stencil_format);
    return hash;
}());
ENG_DEFINE_STD_HASH(eng::gfx::PipelineLayoutCreateInfo, [&t] {
    uint64_t hash = 0;
    // clang-format off
    for(const auto& e : t.sets) 
    { 
        hash = eng::hash::combine_fnv1a(hash, e.flags);
        for(const auto& b : e.bindings) { hash = eng::hash::combine_fnv1a(hash, b.type, b.slot, b.size, b.stages, b.flags, b.immutable_samplers); }
    }
    hash = eng::hash::combine_fnv1a(hash, t.range.stages, t.range.size);
    // clang-format on
    return hash;
}());
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo, [&t] {
    uint64_t hash = 0;
    // clang-format off
    for(const auto& e : t.shaders) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.bindings) { hash = eng::hash::combine_fnv1a(hash, e); }
    for(const auto& e : t.attributes) { hash = eng::hash::combine_fnv1a(hash, e); }
    // clang-format on
    hash = eng::hash::combine_fnv1a(hash, t.layout, t.attachments, t.depth_test, t.depth_write, t.depth_compare, t.stencil_test,
                                    t.stencil_front, t.stencil_back, t.polygon_mode, t.culling, t.front_is_ccw, t.line_width);
    return hash;
}());
ENG_DEFINE_STD_HASH(eng::gfx::PipelineLayout, eng::hash::combine_fnv1a(t.info));
ENG_DEFINE_STD_HASH(eng::gfx::Pipeline, eng::hash::combine_fnv1a(t.info));
ENG_DEFINE_STD_HASH(eng::gfx::Shader, eng::hash::combine_fnv1a(t.path));
ENG_DEFINE_STD_HASH(eng::gfx::Geometry, eng::hash::combine_fnv1a(t.meshlet_range));
ENG_DEFINE_STD_HASH(eng::gfx::Material, eng::hash::combine_fnv1a(t.mesh_pass, t.base_color_texture));
ENG_DEFINE_STD_HASH(eng::gfx::Mesh, eng::hash::combine_fnv1a(t.geometry, t.material));
ENG_DEFINE_STD_HASH(eng::gfx::MeshPass, eng::hash::combine_fnv1a(t.name));
ENG_DEFINE_STD_HASH(eng::gfx::ShaderEffect, eng::hash::combine_fnv1a(t.pipeline));
ENG_DEFINE_STD_HASH(eng::gfx::SamplerDescriptor,
                    eng::hash::combine_fnv1a(t.filtering[0], t.filtering[1], t.addressing[0], t.addressing[1], t.addressing[2],
                                             t.mip_lod[0], t.mip_lod[1], t.mip_lod[2], t.mipmap_mode, t.reduction_mode));
ENG_DEFINE_STD_HASH(eng::gfx::Sampler, eng::hash::combine_fnv1a(t.info));
ENG_DEFINE_STD_HASH(eng::gfx::Texture, eng::hash::combine_fnv1a(t.view, t.layout, t.is_storage));
ENG_DEFINE_STD_HASH(eng::gfx::ImageView, eng::hash::combine_fnv1a(t.image, t.type, t.format, t.aspect.flags, t.mips, t.layers));

namespace eng
{
namespace gfx
{
enum class SubmitFlags : uint32_t
{
};

namespace RenderOrder
{
inline constexpr uint32_t DEFAULT_UNLIT = 50;
inline constexpr uint32_t PRESENT = 100;
} // namespace RenderOrder

class Renderer
{
  public:
    static inline uint32_t frame_count = 2;

    struct InstanceBatch
    {
        Handle<Pipeline> pipeline;
        uint32_t inst_count{};
        uint32_t cmd_count{};
    };

    struct IndirectBatch
    {
        std::vector<InstanceBatch> batches;
        Handle<Buffer> cmd_buf;
        Handle<Buffer> ids_buf;
        uint32_t cmd_count{};
        uint32_t cmd_start{};
        uint32_t ids_count{};
    };

    struct RenderPass
    {
        std::vector<ecs::entity> entities;
        IndirectBatch batch{};
        bool redo{ true };
    };

    struct GBuffer
    {
        Handle<Image> color;
        Handle<Image> depth;
        Handle<Image> hiz_pyramid;
        Handle<Image> hiz_debug_output;
    };

    struct PerFrame
    {
        GBuffer gbuffer;

        CommandPool* cmdpool{};
        Sync* acq_sem{};
        Sync* ren_sem{};
        Sync* swp_sem{};
        Sync* ren_fen{};
        Handle<Buffer> constants{};
    };

    struct GeometryBuffers
    {
        Handle<Buffer> vpos_buf;      // positions
        Handle<Buffer> vattr_buf;     // rest of attributes
        Handle<Buffer> idx_buf;       // indices
        Handle<Buffer> bsphere_buf;   // bounding spheres
        Handle<Buffer> mats_buf;      // materials
        Handle<Buffer> trs_bufs[2]{}; // transforms

        Handle<Buffer> lights_bufs[2]{}; // lights

        static inline constexpr uint32_t fwdp_tile_pixels{ 16 }; // changing would require recompiling compute shader with larger local size
        uint32_t fwdp_lights_per_tile{ 256 }; // changing requires resizing the buffers
        uint32_t fwdp_num_tiles{};

        VkIndexType index_type{ VK_INDEX_TYPE_UINT16 };
        size_t vertex_count{};
        size_t index_count{};
    };

    struct HelperGeometry
    {
        Handle<Geometry> uvsphere;
        Handle<Pipeline> ppskybox;
    };

    void init(RendererBackend* backend);
    void init_helper_geom();
    void init_pipelines();
    void init_perframes();
    void init_bufs();
    void init_rgraph_passes();

    void instance_entity(ecs::entity e);

    void update();
    void render(RenderPassType pass, SubmitQueue* queue, CommandBuffer* cmd);
    void build_renderpasses();
    void render_ibatch(CommandBuffer* cmd, const IndirectBatch& ibatch,
                       const Callback<void(CommandBuffer*)>& setup_resources, bool bind_pps = true);

    Handle<Buffer> make_buffer(const BufferDescriptor& info);
    Handle<Image> make_image(const ImageDescriptor& info);
    Handle<ImageView> make_view(const ImageViewDescriptor& info);
    Handle<Sampler> make_sampler(const SamplerDescriptor& info);
    Handle<Shader> make_shader(const std::filesystem::path& path);
    Handle<PipelineLayout> make_pplayout(const PipelineLayoutCreateInfo& info);
    Handle<Pipeline> make_pipeline(const PipelineCreateInfo& info);
    Sync* make_sync(const SyncCreateInfo& info);
    void destroy_sync(Sync* sync);
    Handle<Texture> make_texture(const TextureDescriptor& info);
    Handle<Material> make_material(const MaterialDescriptor& info);
    Handle<Geometry> make_geometry(const GeometryDescriptor& info);
    static void meshletize_geometry(const GeometryDescriptor& info, std::vector<float>& out_vertices,
                                    std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets);
    Handle<Mesh> make_mesh(const MeshDescriptor& info);
    Handle<ShaderEffect> make_shader_effect(const ShaderEffect& info);
    Handle<MeshPass> make_mesh_pass(const MeshPassCreateInfo& info);
    Handle<DescriptorPool> make_descpool(const DescriptorPoolCreateInfo& info);

    // void instance_blas(const BLASInstanceSettings& settings);
    void update_transform(ecs::entity entity);

    SubmitQueue* get_queue(QueueType type);
    uint32_t get_bindless(Handle<Buffer> buffer, Range range = { 0ull, ~0ull });
    uint32_t get_bindless(Handle<Texture> texture);
    uint32_t get_bindless(Handle<Sampler> sampler);

    uint32_t get_perframe_index(int32_t offset = 0);
    PerFrame& get_perframe(int32_t offset = 0);

    SubmitQueue* gq{};
    Swapchain* swapchain{};
    RendererBackend* backend{};
    StagingBuffer* sbuf{};
    BindlessPool* bindless{};
    RenderGraph* rgraph{};
    RenderGraphPasses* rgraphpasses{};

    enum class DebugOutput
    {
        COLOR,
        FWDP_GRID
    };
    DebugOutput debug_output{};
    bool fwdp_enable{ true };
    bool mlt_occ_cull_enable{ true };
    bool mlt_frust_cull_enable{ true };

    HandleSparseVec<Buffer> buffers;
    HandleSparseVec<Image> images;
    HandleSparseVec<Sampler> samplers;
    HandleFlatSet<ImageView> image_views;
    std::unordered_map<Handle<Image>, std::vector<Handle<ImageView>>> image_views_cache;
    HandleFlatSet<Shader> shaders;
    std::vector<Handle<Shader>> new_shaders;
    HandleFlatSet<PipelineLayout, std::hash<PipelineLayout>,
                  decltype([](const PipelineLayout& a, const PipelineLayout& b) -> bool { return a.is_compatible(b); })>
        pplayouts;
    HandleFlatSet<Pipeline> pipelines;
    std::vector<Handle<Pipeline>> new_pipelines;
    std::vector<Meshlet> meshlets;
    std::vector<Mesh> meshes;

    HandleSparseVec<Geometry> geometries;
    HandleFlatSet<ShaderEffect> shader_effects;
    HandleFlatSet<MeshPass> mesh_passes;
    HandleFlatSet<Texture> textures;
    HandleFlatSet<Material> materials;
    std::vector<DescriptorPool> descpools;
    std::vector<Handle<Material>> new_materials;
    std::vector<ecs::entity> new_transforms;
    ecs::View<ecs::Transform, ecs::Mesh> ecs_mesh_view;
    ecs::View<ecs::Light> ecs_light_view;
    std::unordered_map<RenderPassType, RenderPass> render_passes;
    std::vector<ecs::entity> new_lights;

    GeometryBuffers bufs;
    SlotAllocator gpu_resource_allocator;
    SlotAllocator gpu_light_allocator;
    std::vector<Sync*> syncs;
    Handle<DescriptorPool> bindless_pool;
    Handle<DescriptorSet> bindless_set;
    Handle<PipelineLayout> bindless_pplayout;
    Handle<Pipeline> default_unlit_pipeline;
    Handle<MeshPass> default_meshpass;
    Handle<Material> default_material;
    Handle<Pipeline> hiz_pipeline;
    // Handle<Sampler> hiz_sampler; // moved to immutable samplers
    Handle<Pipeline> cull_pipeline;
    Handle<Pipeline> cullzout_pipeline;
    Handle<Pipeline> fwdp_cull_lights_pipeline;
    ImGuiRenderer* imgui_renderer{};
    std::vector<PerFrame> perframe;
    HelperGeometry helpergeom;
};
} // namespace gfx
} // namespace eng
