#pragma once

#include <cstdint>
#include <string_view>
#include <span>
#include <compare>
#include <memory>
#include <utility>
#include <array>
#include <filesystem>
#include <unordered_set>
#include <glm/glm.hpp>
#include <eng/engine.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/slotmap.hpp>
#include <eng/common/handleflatset.hpp>
#include <eng/common/callback.hpp>
#include <eng/common/slotallocator.hpp>
#include <eng/ecs/components.hpp>

ENG_DEFINE_STD_HASH(eng::gfx::ImageView, eng::hash::combine_fnv1a(t.image, t.type, t.format, t.src_subresource, t.dst_subresource));
ENG_DEFINE_STD_HASH(eng::gfx::BufferView, eng::hash::combine_fnv1a(t.buffer, t.range));

namespace eng
{
namespace gfx
{

struct DescriptorResource
{
    static DescriptorResource as_sampled(uint32_t binding, const ImageView& view, uint32_t index = 0)
    {
        return DescriptorResource{ .type = DescriptorType::SAMPLED_IMAGE, .image_view = view, .binding = binding, .index = index };
    }
    static DescriptorResource as_storage(uint32_t binding, Handle<Buffer> buffer, uint32_t index = 0)
    {
        return as_storage(binding, BufferView::init(buffer), index);
    }
    static DescriptorResource as_storage(uint32_t binding, const BufferView& view, uint32_t index = 0)
    {
        return DescriptorResource{ .type = DescriptorType::STORAGE_BUFFER, .buffer_view = view, .binding = binding, .index = index };
    }
    static DescriptorResource as_storage(uint32_t binding, const ImageView& view, uint32_t index = 0)
    {
        return DescriptorResource{ .type = DescriptorType::STORAGE_IMAGE, .image_view = view, .binding = binding, .index = index };
    }
    DescriptorType type{};
    union {
        BufferView buffer_view;
        ImageView image_view;
    };
    uint32_t binding{ ~0u };
    uint32_t index{ ~0u };
};

struct ImageBlockData
{
    uint32_t bytes_per_texel;
    Vec3u32 texel_extent;
};

ImageBlockData get_block_data(ImageFormat format);

struct Shader
{
    auto operator==(const Shader& o) const { return path == o.path; }
    std::filesystem::path path;
    ShaderStage stage{ ShaderStage::NONE };
    union Metadata {
        ShaderMetadataVk* vk{};
    } md;
};

struct Descriptor
{
    auto operator<=>(const Descriptor& a) const = default;
    DescriptorType type{};
    uint32_t slot{};
    uint32_t size{};
    Flags<ShaderStage> stages{};
    const Handle<Sampler>* immutable_samplers{};
};

struct DescriptorLayout
{
    bool operator==(const DescriptorLayout& a) const { return layout == a.layout; }
    bool is_compatible(const DescriptorLayout& a) const;
    std::vector<Descriptor> layout;
    union Metadata {
        DescriptorLayoutMetadataVk* vk{};
    } md;
};

struct PushRange
{
    inline static constexpr auto MAX_PUSH_BYTES = 128u;
    auto operator<=>(const PushRange& a) const = default;
    Flags<ShaderStage> stages{};
    uint32_t size{};
};

struct PipelineLayout
{
    inline static Handle<PipelineLayout> common_layout;
    bool operator==(const PipelineLayout& a) const { return layout == a.layout && push_range == a.push_range; }
    bool is_compatible(const PipelineLayout& a) const;
    std::vector<Handle<DescriptorLayout>> layout{};
    PushRange push_range{};
    union Metadata {
        PipelineLayoutMetadataVk* vk{};
    } md;
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
        bool operator==(const AttachmentState& o) const
        {
            if(count != o.count) { return false; }
            if(depth_format != o.depth_format) { return false; }
            if(stencil_format != o.stencil_format) { return false; }
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
    Handle<PipelineLayout> layout; // optional

    AttachmentState attachments;
    bool depth_test{ false };
    bool depth_write{ false };
    DepthCompare depth_compare{ DepthCompare::NEVER };
    bool stencil_test{ false };
    StencilState stencil_front;
    StencilState stencil_back;

    Topology topology{ Topology::TRIANGLE_LIST };
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
        PipelineMetadataVk* vk{};
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

struct MeshPass
{
    static MeshPass init(const std::string& name) { return MeshPass{ .name = name }; }
    MeshPass& set(RenderPassType type, Handle<ShaderEffect> effect)
    {
        effects[(int)type] = effect;
        return *this;
    }
    bool operator==(const MeshPass& o) const { return name == o.name; }
    std::string name;
    std::array<Handle<ShaderEffect>, (uint32_t)RenderPassType::LAST_ENUM> effects;
};

struct Material
{
    auto operator<=>(const Material& t) const = default;
    Handle<MeshPass> mesh_pass;
    ImageView base_color_texture;
};

struct Mesh
{
    bool operator==(const Mesh& a) const { return geometry == a.geometry && material == a.material; }
    Handle<Geometry> geometry;
    Handle<Material> material;
    uint32_t gpu_resource{ ~0u }; // renderer sets this when it processeed the mesh
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
    // size_t get_num_indices() const { return indices.size_bytes() / get_index_size(gfx::IndexFormat::U16); }
    size_t get_num_vertices() const
    {
        return vertices.size() * sizeof(vertices[0]) / get_vertex_layout_size(vertex_layout);
    }
    Flags<GeometryFlags> flags;
    Flags<VertexComponent> vertex_layout;
    std::span<const float> vertices;
    std::span<const uint32_t> indices;
};

struct Buffer
{
    static Buffer init(size_t capacity, Flags<BufferUsage> usage)
    {
        return Buffer{ .usage = usage, .capacity = capacity, .size = 0ull, .memory = {} };
    }

    Flags<BufferUsage> usage{};
    size_t capacity{};
    size_t size{};
    void* memory{};
    struct Metadata
    {
        BufferMetadataVk* as_vk() const { return (BufferMetadataVk*)ptr; }
        void* ptr{};
    } md;
};

struct Image
{
    static Image init(uint32_t width, uint32_t height, ImageFormat format, Flags<ImageUsage> usage,
                      ImageLayout layout = ImageLayout::UNDEFINED)
    {
        return init(width, height, 1, format, usage, (uint32_t)(std::log2f((float)std::min(width, height)) + 1), 1, layout);
    }
    static Image init(uint32_t width, uint32_t height, uint32_t depth, ImageFormat format, Flags<ImageUsage> usage,
                      uint32_t mips = 1, uint32_t layers = 1, ImageLayout layout = ImageLayout::UNDEFINED)
    {
        return Image{
            .type = depth > 1    ? ImageType::TYPE_3D
                    : height > 1 ? ImageType::TYPE_2D
                                 : ImageType::TYPE_1D,
            .format = format,
            .width = width,
            .height = std::max(height, 1u),
            .depth = std::max(depth, 1u),
            .mips = mips == 0u ? (uint32_t)(std::log2f((float)std::min(width, height)) + 1) : mips,
            .layers = layers,
            .usage = usage,
            .layout = layout,
        };
    }

    ImageType type{ ImageType::TYPE_2D };
    ImageFormat format{};
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1u };
    uint32_t mips{ 1u };
    uint32_t layers{ 1u };
    Flags<ImageUsage> usage{ ImageUsage::NONE };
    ImageLayout layout{ ImageLayout::UNDEFINED };
    struct Metadata
    {
        ImageMetadataVk* as_vk() const { return (ImageMetadataVk*)ptr; }
        void* ptr{};
    } md;
};

struct ImageMipLayerRange
{
    Range32u mips{};
    Range32u layers{};
};

struct ImageLayerRange
{
    uint32_t mip{};
    Range32u layers{};
};

struct ImageBlit
{
    ImageLayerRange srclayers{};
    ImageLayerRange dstlayers{};
    Range3D32i srcrange{};
    Range3D32i dstrange{};
    ImageFilter filter{ ImageFilter::LINEAR };
};

struct ImageCopy
{
    ImageLayerRange srclayers{};
    ImageLayerRange dstlayers{};
    Vec3i32 srcoffset{};
    Vec3i32 dstoffset{};
    Vec3u32 extent{};
};

struct Sampler
{
    static Sampler init(ImageFilter filtering, ImageAddressing addressing)
    {
        return init(filtering, filtering, addressing, addressing, addressing);
    }
    static Sampler init(ImageFilter min, ImageFilter mag, ImageAddressing u, ImageAddressing v, ImageAddressing w,
                        SamplerMipmapMode mip_blending = SamplerMipmapMode::LINEAR, float lod_min = 0.0f, float lod_max = 1000.0f,
                        float lod_base = 0.0f, SamplerReductionMode reduction = SamplerReductionMode::NONE)
    {
        return Sampler{
            .filtering = { min, mag },
            .addressing = { u, v, w },
            .mip_blending = mip_blending,
            .reduction_mode = reduction,
            .lod = { lod_min, lod_max, lod_base },
        };
    }
    bool operator==(const Sampler& a) const
    {
        return filtering == a.filtering && addressing == a.addressing && mip_blending == a.mip_blending &&
               reduction_mode == a.reduction_mode && lod == a.lod;
    }
    struct Filtering
    {
        auto operator<=>(const Filtering&) const = default;
        ImageFilter min{ ImageFilter::LINEAR };
        ImageFilter mag{ ImageFilter::LINEAR };
    } filtering;
    struct Addressing
    {
        auto operator<=>(const Addressing&) const = default;
        ImageAddressing u{ ImageAddressing::REPEAT };
        ImageAddressing v{ ImageAddressing::REPEAT };
        ImageAddressing w{ ImageAddressing::REPEAT };
    } addressing;
    SamplerMipmapMode mip_blending{ SamplerMipmapMode::LINEAR };
    SamplerReductionMode reduction_mode{ SamplerReductionMode::NONE };
    struct Lod
    {
        auto operator<=>(const Lod&) const = default;
        float min{ 0.0f };
        float max{ 1000.0f };
        float bias{ 0.0f };
    } lod;
    struct Metadata
    {
        SamplerMetadataVk* as_vk() const { return (SamplerMetadataVk*)ptr; }
        void* ptr{};
    } md;
};

struct MaterialDescriptor
{
    std::string mesh_pass{ "default_unlit" };
    ImageView base_color_texture;
    ImageView normal_texture;
    ImageView metallic_roughness_texture;
};

struct MeshDescriptor
{
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct InstanceSettings
{
    ecs::EntityId entity;
};

struct BLASInstanceSettings
{
    ecs::EntityId entity;
};

// struct VsmData
//{
//     Handle<Buffer> constants_buffer;
//     Handle<Buffer> free_allocs_buffer;
//     Handle<Image> shadow_map_0;
//     // VkImageView view_shadow_map_0_general{};
//     Handle<Image> dir_light_page_table;
//     // VkImageView view_dir_light_page_table_general{};
//     Handle<Image> dir_light_page_table_rgb8;
//     // VkImageView view_dir_light_page_table_rgb8_general{};
// };

struct DebugGeometry
{
    enum class Type
    {
        NONE,
        AABB,
    };

    static DebugGeometry init_aabb(glm::vec3 a, glm::vec3 b)
    {
        return DebugGeometry{ .type = Type::AABB, .data = { .aabb = { a, b } } };
    }

    Type type{ Type::NONE };
    union {
        struct AABB
        {
            glm::vec3 a, b;
        } aabb;
    } data;
};

template <typename T> struct LayoutCompatibilityChecker
{
    bool operator()(const T& a, const T& b) const noexcept { return a.is_compatible(b); }
};

struct RendererBackendCaps
{
    bool supports_bindless{};
};

struct RendererMemoryRequirements
{
    auto operator<=>(const RendererMemoryRequirements&) const = default;
    size_t size{};
    size_t alignment{};
    std::array<uint32_t, 8> backend_data{}; // for storing additional backend-specific data (vulkan uses it to store memory type bits)
};

class IRendererBackend
{
  public:
    virtual ~IRendererBackend() = default;

    virtual void init() = 0;

    virtual void allocate_buffer(Buffer& buffer, AllocateMemory alloc = AllocateMemory::YES) = 0;
    virtual void destroy_buffer(Buffer& buffer) = 0;
    virtual void allocate_image(Image& image, AllocateMemory alloc = AllocateMemory::YES, void* user_data = nullptr) = 0;
    virtual void destroy_image(Image& b) = 0;
    virtual void allocate_view(const ImageView& view, void** out_allocation) = 0;
    virtual void allocate_sampler(Sampler& sampler) = 0;
    virtual void make_shader(Shader& shader) = 0;
    virtual bool compile_shader(const Shader& shader) = 0;
    virtual bool compile_layout(DescriptorLayout& layout) = 0;
    virtual bool compile_layout(PipelineLayout& layout) = 0;
    virtual void make_pipeline(Pipeline& pipeline) = 0;
    virtual bool compile_pipeline(const Pipeline& pipeline) = 0;
    virtual Sync* make_sync(const SyncCreateInfo& info) = 0;
    virtual void destory_sync(Sync*) = 0;
    virtual Swapchain* make_swapchain() = 0;
    virtual SubmitQueue* get_queue(QueueType type) = 0;

    virtual ImageView::Metadata get_md(const ImageView& view) = 0;

    virtual size_t get_indirect_indexed_command_size() const = 0;
    virtual void make_indirect_indexed_command(void* out, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                               int32_t first_vertex, uint32_t first_instance) const = 0;

    // Gets requirements for a resource. Passing same reqs pointer multiple times accumulates requirements: max(size), max(alignment)
    virtual void get_memory_requirements(const Buffer& resource, RendererMemoryRequirements& reqs) = 0;
    // Gets requirements for a resource. Passing same reqs pointer multiple times accumulates requirements: max(size), max(alignment)
    virtual void get_memory_requirements(const Image& resource, RendererMemoryRequirements& reqs) = 0;
    // Allocates aliasable memory based on memory requiremets built from the set of resources that want to share the memory.
    // Returns null if the resources cannot be in the same memory (possibly due to memory heap not supporing all the resources).
    virtual void* allocate_aliasable_memory(const RendererMemoryRequirements& reqs) = 0;
    virtual void bind_aliasable_memory(Buffer& resource, void* memory, size_t offset) = 0;
    virtual void bind_aliasable_memory(Image& resource, void* memory, size_t offset) = 0;

    virtual void set_debug_name(Buffer& resource, std::string_view name) const = 0;
    virtual void set_debug_name(Image& resource, std::string_view name) const = 0;

    RendererBackendCaps caps{};
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
                                             t.src_alpha_factor, t.dst_alpha_factor, t.alpha_op, t.r > 0 ? true : false,
                                             t.g > 0 ? true : false, t.b > 0 ? true : false, t.a > 0 ? true : false));
ENG_DEFINE_STD_HASH(
    eng::gfx::PipelineCreateInfo::AttachmentState,
    eng::hash::combine_fnv1a(t.count, t.depth_format, t.stencil_format,
                             std::accumulate(t.color_formats.begin(), t.color_formats.begin() + t.count, 0ull,
                                             [](auto acc, const auto& e) { return eng::hash::combine_fnv1a(acc, e); }),
                             std::accumulate(t.blend_states.begin(), t.blend_states.begin() + t.count, 0ull,
                                             [](auto acc, const auto& e) { return eng::hash::combine_fnv1a(acc, e); })));
ENG_DEFINE_STD_HASH(eng::gfx::Descriptor, eng::hash::combine_fnv1a(t.type, t.slot, t.size, t.stages, t.immutable_samplers));
ENG_DEFINE_STD_HASH(eng::gfx::DescriptorLayout,
                    eng::hash::combine_fnv1a(std::accumulate(t.layout.begin(), t.layout.end(), 0ull, [](auto hash, const auto& val) {
                        return eng::hash::combine_fnv1a(hash, val);
                    })));
ENG_DEFINE_STD_HASH(
    eng::gfx::PipelineCreateInfo,
    eng::hash::combine_fnv1a(t.layout, t.attachments, t.depth_test, t.depth_write, t.depth_compare, t.stencil_test,
                             t.stencil_front, t.stencil_back, t.topology, t.polygon_mode, t.culling, t.front_is_ccw, t.line_width,
                             std::accumulate(t.shaders.begin(), t.shaders.end(), 0ull,
                                             [](auto acc, const auto& e) { return eng::hash::combine_fnv1a(acc, e); }),
                             std::accumulate(t.bindings.begin(), t.bindings.end(), 0ull,
                                             [](auto acc, const auto& e) { return eng::hash::combine_fnv1a(acc, e); }),
                             std::accumulate(t.attributes.begin(), t.attributes.end(), 0ull,
                                             [](auto acc, const auto& e) { return eng::hash::combine_fnv1a(acc, e); })));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineLayout,
                    eng::hash::combine_fnv1a(t.push_range.stages, t.push_range.size,
                                             std::accumulate(t.layout.begin(), t.layout.end(), 0ull, [](auto hash, const auto& val) {
                                                 return eng::hash::combine_fnv1a(hash, val);
                                             })));
ENG_DEFINE_STD_HASH(eng::gfx::Pipeline, eng::hash::combine_fnv1a(t.info));
ENG_DEFINE_STD_HASH(eng::gfx::Shader, eng::hash::combine_fnv1a(t.path));
ENG_DEFINE_STD_HASH(eng::gfx::Geometry, eng::hash::combine_fnv1a(t.meshlet_range));
ENG_DEFINE_STD_HASH(eng::gfx::Material, eng::hash::combine_fnv1a(t.mesh_pass, t.base_color_texture));
ENG_DEFINE_STD_HASH(eng::gfx::Mesh, eng::hash::combine_fnv1a(t.geometry, t.material));
ENG_DEFINE_STD_HASH(eng::gfx::MeshPass, eng::hash::combine_fnv1a(t.name));
ENG_DEFINE_STD_HASH(eng::gfx::ShaderEffect, eng::hash::combine_fnv1a(t.pipeline));
// ENG_DEFINE_STD_HASH(eng::gfx::SamplerDescriptor,
//                     eng::hash::combine_fnv1a(t.filtering[0], t.filtering[1], t.addressing[0], t.addressing[1], t.addressing[2],
//                                              t.mip_lod[0], t.mip_lod[1], t.mip_lod[2], t.mipmap_mode, t.reduction_mode));
ENG_DEFINE_STD_HASH(eng::gfx::Sampler, eng::hash::combine_fnv1a(t.filtering.min, t.filtering.mag, t.addressing.u,
                                                                t.addressing.v, t.addressing.w, t.mip_blending,
                                                                t.reduction_mode, t.lod.min, t.lod.max, t.lod.bias));
//  ENG_DEFINE_STD_HASH(eng::gfx::Texture, eng::hash::combine_fnv1a(t.view, t.layout, t.is_storage));
ENG_DEFINE_STD_HASH(eng::gfx::RendererMemoryRequirements,
                    eng::hash::combine_fnv1a(t.size, t.alignment,
                                             std::accumulate(t.backend_data.begin(), t.backend_data.end(), 0ull,
                                                             [](auto acc, const auto& e) {
                                                                 return eng::hash::combine_fnv1a(acc, e);
                                                             })));

namespace eng
{
namespace gfx
{

struct Swapchain
{
    using acquire_impl_fptr = uint32_t (*)(Swapchain* a, uint64_t timeout, Sync* semaphore, Sync* fence);
    static inline acquire_impl_fptr acquire_impl{};
    uint32_t acquire(uint64_t timeout = -1ull, Sync* semaphore = nullptr, Sync* fence = nullptr);
    Handle<Image> get_image() const;
    ImageView get_view() const;
    void* metadata{};
    std::vector<Handle<Image>> images;
    std::vector<ImageView> views;
    uint32_t current_index{ 0ul };
};

enum class SubmitFlags : uint32_t
{
};

enum class RenderOrder
{
    DEFAULT_UNLIT,
    PRESENT
};

class Renderer
{
  public:
    static inline uint32_t frame_delay = 2;

    struct InstanceBatch
    {
        Handle<Pipeline> pipeline;
        uint32_t instance_count;
        uint32_t first_command;
        uint32_t command_count;
    };

    struct IndirectBatch;
    struct IndirectDrawParams
    {
        const IndirectBatch* batch{};
        const InstanceBatch* draw{};
        uint32_t max_draw_count{};
    };

    struct IndirectBatch
    {
        void draw(const Callback<void(const IndirectDrawParams&)>& draw_callback) const;
        std::vector<InstanceBatch> batches;
        Handle<Buffer> indirect_buf; // [counts..., commands...]
        BufferView counts_view;
        BufferView cmds_view;
    };

    struct MeshInstance
    {
        Handle<Geometry> geometry;
        Handle<Material> material;
        uint32_t instance_index;
        uint32_t meshlet_index;
    };

    struct RenderPass
    {
        void clear()
        {
            // entities.clear();
            mesh_instances.clear();
            draw.batches.clear();
        }
        IndirectBatch draw;
        Handle<Buffer> instance_buffer;
        BufferView instance_view;
        // std::vector<ecs::EntityId> entities;
        std::vector<MeshInstance> mesh_instances;
    };

    struct GBuffer
    {
        Handle<Image> color;
        Handle<Image> depth;
        // Handle<Image> hiz_pyramid;
        // Handle<Image> hiz_debug_output;
    };

    struct FrameData
    {
        GBuffer gbuffer;

        CommandPoolVk* cmdpool{};
        Sync* acq_sem{};
        Sync* ren_sem{};
        Sync* swp_sem{};
        Sync* ren_fen{};
        Handle<Buffer> constants{};

        struct RetiredResource
        {
            std::variant<Handle<Buffer>, Handle<Image>> resource;
            size_t deleted_at_frame{};
        };
        std::vector<RetiredResource> retired_resources;
    };

    struct DebugGeomBuffers
    {
        void render(CommandBufferVk* cmd, Sync* s);
        void add(const DebugGeometry& geom) { geometry.push_back(geom); }

      private:
        std::vector<glm::vec3> expand_into_vertices();

        Handle<Buffer> vpos_buf;
        std::vector<DebugGeometry> geometry;
    };

    struct GeometryBuffers
    {
        Handle<Buffer> positions;  // positions
        Handle<Buffer> attributes; // rest of attributes
        Handle<Buffer> indices;    // indices
        Handle<Buffer> bspheres;   // bounding spheres
        Handle<Buffer> materials;  // materials

        Handle<Buffer> transforms[2]; // transforms
        Handle<Buffer> lights[2];     // lights

        static inline constexpr uint32_t fwdp_tile_pixels{ 16 }; // changing would require recompiling compute shader with larger local size
        uint32_t fwdp_lights_per_tile{ 256 }; // changing requires resizing the buffers
        uint32_t fwdp_num_tiles{};

        VkIndexType index_type{ VK_INDEX_TYPE_UINT16 };
        size_t vertex_count{};
        size_t index_count{};
        // size_t transform_count{};
        // size_t light_count{};
    };

    void init(IRendererBackend* backend);
    void init_helper_geom();
    void init_pipelines();
    void init_perframes();
    void init_bufs();
    void init_rgraph_passes();

    void update();
    void build_renderpasses();
    void render_debug(const DebugGeometry& geom);

    Handle<Buffer> make_buffer(std::string_view name, Buffer&& buffer, AllocateMemory allocate = AllocateMemory::YES);
    void destroy_buffer(Handle<Buffer>& handle);
    Handle<Image> make_image(std::string_view name, Image&& image, AllocateMemory allocate = AllocateMemory::YES,
                             void* user_data = nullptr);
    void destroy_image(Handle<Image>& image);
    Handle<Sampler> make_sampler(Sampler&& sampler);
    Handle<Shader> make_shader(const std::filesystem::path& path);
    Handle<DescriptorLayout> make_layout(const DescriptorLayout& info);
    Handle<PipelineLayout> make_layout(const PipelineLayout& info);
    Handle<Pipeline> make_pipeline(const PipelineCreateInfo& info);
    Sync* make_sync(const SyncCreateInfo& info);
    void destroy_sync(Sync* sync);
    // Handle<Texture> make_texture(const TextureDescriptor& info);
    Handle<Material> make_material(const MaterialDescriptor& info);
    Handle<Geometry> make_geometry(const GeometryDescriptor& info);
    static void meshletize_geometry(const GeometryDescriptor& info, std::vector<float>& out_vertices,
                                    std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets);
    Handle<Mesh> make_mesh(const MeshDescriptor& info);
    Handle<ShaderEffect> make_shader_effect(const ShaderEffect& info);
    Handle<MeshPass> make_mesh_pass(const MeshPass& info);

    void resize_buffer(Handle<Buffer>& handle, size_t new_size, bool copy_data);
    void resize_buffer(Handle<Buffer>& handle, size_t upload_size, size_t offset, bool copy_data);

    // void instance_blas(const BLASInstanceSettings& settings);
    // void update_transform(ecs::entity_id entity);

    SubmitQueue* get_queue(QueueType type);

    FrameData& get_framedata(int32_t offset = 0);

    SubmitQueue* gq{};
    Swapchain* swapchain{};
    IRendererBackend* backend{};
    StagingBuffer* staging{};

    RenderGraph* rgraph{};
    uint32_t imgui_input; // todo: this is very temp
    std::vector<pass::IPass*> rgraph_passes;

    enum class DebugOutput
    {
        COLOR,
        FWDP_GRID
    };
    DebugOutput debug_output{};
    bool fwdp_enable{ true };
    bool mlt_occ_cull_enable{ true };
    bool mlt_frust_cull_enable{ true };

    Slotmap<Buffer, 1024> buffers;
    Slotmap<Image, 1024> images;
    std::vector<std::string> buffer_names;
    // std::vector<std::string> image_names;

    HandleFlatSet<Sampler> samplers;
    HandleFlatSet<Shader> shaders;
    std::vector<Handle<Shader>> new_shaders;
    HandleFlatSet<DescriptorLayout, std::hash<DescriptorLayout>, LayoutCompatibilityChecker<DescriptorLayout>> dlayouts;
    HandleFlatSet<PipelineLayout, std::hash<PipelineLayout>, LayoutCompatibilityChecker<PipelineLayout>> pplayouts;
    HandleFlatSet<Pipeline> pipelines;
    std::vector<Handle<Pipeline>> new_pipelines;
    std::vector<Meshlet> meshlets;
    std::vector<Mesh> meshes;

    std::vector<Geometry> geometries;
    HandleFlatSet<ShaderEffect> shader_effects;
    HandleFlatSet<MeshPass> mesh_passes;
    // HandleFlatSet<Texture> textures;
    HandleFlatSet<Material> materials;
    std::vector<Handle<Material>> new_materials;
    std::vector<ecs::EntityId> new_transforms;
    std::array<RenderPass, (int)RenderPassType::LAST_ENUM> render_passes;
    std::vector<ecs::EntityId> new_lights;

    GeometryBuffers bufs;
    DebugGeomBuffers debug_bufs;
    SlotAllocator gpu_resource_allocator;
    SlotAllocator gpu_light_allocator;
    std::vector<Sync*> syncs;
    IDescriptorSetAllocator* descriptor_allocator{};
    Handle<Pipeline> default_unlit_pipeline;
    Handle<MeshPass> default_meshpass;
    Handle<Material> default_material;
    ImGuiRenderer* imgui_renderer{};
    std::vector<FrameData> perframe;
    uint64_t current_frame{}; // monotonically increasing counter
};

inline Renderer& get_renderer() { return *::eng::Engine::get().renderer; }

} // namespace gfx

// clang-format off
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Buffer, { return &::eng::gfx::get_renderer().buffers.at(SlotIndex<uint32_t>{*handle}); });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Image, { return &::eng::gfx::get_renderer().images.at(SlotIndex<uint32_t>{*handle}); });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Geometry, { return &::eng::gfx::get_renderer().geometries[*handle]; });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Mesh, { return &::eng::gfx::get_renderer().meshes[*handle]; });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Shader, { return &::eng::gfx::get_renderer().shaders.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Sampler, { return &::eng::gfx::get_renderer().samplers.at(handle); });
//ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::BufferView, { return &::eng::gfx::get_renderer().buffer_views.at(handle); });
//ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::ImageView, { return &::eng::gfx::get_renderer().image_views.at(handle); });
//ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Texture);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Material, { return &::eng::gfx::get_renderer().materials.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::DescriptorLayout, { return &::eng::gfx::get_renderer().dlayouts.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::PipelineLayout, { return &::eng::gfx::get_renderer().pplayouts.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Pipeline, { return &::eng::gfx::get_renderer().pipelines.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::MeshPass, { return &::eng::gfx::get_renderer().mesh_passes.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::ShaderEffect, { return &::eng::gfx::get_renderer().shader_effects.at(handle); });
// clang-format on

} // namespace eng
