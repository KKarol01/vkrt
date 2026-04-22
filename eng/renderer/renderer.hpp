#pragma once

#include <mutex>
#include <unordered_set>
#include <glm/glm.hpp>
#include <eng/engine.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/renderer/passes/renderpass.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/renderer/backend.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/slotmap.hpp>
#include <eng/common/handleflatset.hpp>
#include <eng/common/slotallocator.hpp>
#include <eng/ecs/components.hpp>
#include <eng/assets/asset_manager.hpp>
#include <eng/fs/fs.hpp>

ENG_DEFINE_STD_HASH(eng::gfx::ImageView, ENG_HASH(t.image, t.type, t.format, t.src_subresource, t.dst_subresource));
ENG_DEFINE_STD_HASH(eng::gfx::BufferView, ENG_HASH(t.buffer, t.range));

namespace eng
{
namespace gfx
{

namespace pass
{
struct Pass;
}

class Renderer;

inline Renderer& get_renderer() { return *eng::get_engine().renderer; }

struct DescriptorResource
{
    static DescriptorResource sampled_image(const ImageView& view, uint32_t index = 0)
    {
        return DescriptorResource{ view, DescriptorType::SAMPLED_IMAGE, index };
    }
    static DescriptorResource sampled_image(Handle<Image> image, uint32_t index = 0)
    {
        return sampled_image(ImageView::init(image), index);
    }
    static DescriptorResource storage_image(const ImageView& view, uint32_t index = 0)
    {
        return DescriptorResource{ view, DescriptorType::STORAGE_IMAGE, index };
    }

    static DescriptorResource storage_buffer(const BufferView& view, uint32_t index = 0)
    {
        return DescriptorResource{ view, DescriptorType::STORAGE_BUFFER, index };
    }
    static DescriptorResource storage_buffer(Handle<Buffer> buffer, uint32_t index = 0)
    {
        return storage_buffer(BufferView::init(buffer), index);
    }

    static DescriptorResource acceleration_struct(TopAccelerationStructure tlas, uint32_t index = 0)
    {
        return DescriptorResource{ tlas, DescriptorType::ACCELERATION_STRUCTURE, index };
    }

    operator bool() const { return !is_empty(); }

    bool is_empty() const { return resource.index() == 0; }
    bool is_buffer() const { return resource.index() == 1; }
    bool is_image() const { return resource.index() == 2; }
    bool is_tlas() const { return resource.index() == 3; }
    const BufferView& as_buffer() const { return std::get<1>(resource); }
    const ImageView& as_image() const { return std::get<2>(resource); }
    const TopAccelerationStructure& as_tlas() const { return std::get<3>(resource); }

    std::variant<std::monostate, BufferView, ImageView, TopAccelerationStructure> resource;
    DescriptorType type{};
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
    static Shader init(const std::filesystem::path& path)
    {
        const auto stage = [&path] {
            const auto ext = std::filesystem::path{ path }.replace_extension().extension();
            ShaderStage stage{ ShaderStage::NONE };
            if(ext == ".vs") { stage = ShaderStage::VERTEX_BIT; }
            else if(ext == ".ps") { stage = ShaderStage::PIXEL_BIT; }
            else if(ext == ".cs") { stage = ShaderStage::COMPUTE_BIT; }
            else { ENG_WARN("Unrecognized shader extension: {}", ext.string()); }
            return stage;
        }();
        return Shader{ path, stage, {} };
    }
    std::filesystem::path path;
    ShaderStage stage{ ShaderStage::NONE };
    struct Metadata
    {
        ShaderMetadataVk* vk() const { return (ShaderMetadataVk*)ptr; }
        void* ptr{};
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
    inline static constexpr uint32_t MAX_SETS = 4u;
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
        StencilOp fail{};
        StencilOp pass{};
        StencilOp depth_fail{};
        CompareOp compare{};
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

    static PipelineCreateInfo init(const std::vector<fs::Path>& shaders, Handle<PipelineLayout> layout = {});
    PipelineCreateInfo& init_vertex_layout(const std::vector<VertexBinding> bindings, const std::vector<VertexAttribute> attributes)
    {
        this->bindings = { bindings.begin(), bindings.end() };
        this->attributes = { attributes.begin(), attributes.end() };
        return *this;
    }
    PipelineCreateInfo& init_image_attachments(AttachmentState attachments)
    {
        this->attachments = attachments;
        return *this;
    }
    PipelineCreateInfo& init_depth_test(bool depth_test, bool depth_write, DepthCompare depth_compare)
    {
        this->depth_test = depth_test;
        this->depth_write = depth_write;
        this->depth_compare = depth_compare;
        return *this;
    }
    PipelineCreateInfo& init_stencil_test(bool stencil_test, StencilState front, StencilState back)
    {
        this->stencil_test = stencil_test;
        this->stencil_front = front;
        this->stencil_back = back;
        return *this;
    }
    PipelineCreateInfo& init_topology(Topology topology, PolygonMode polygon_mode, CullFace culling,
                                      bool front_is_ccw = true, float line_width = 1.0f)
    {
        this->topology = topology;
        this->polygon_mode = polygon_mode;
        this->culling = culling;
        this->front_is_ccw = front_is_ccw;
        this->line_width = line_width;
        return *this;
    }

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
    struct Metadata
    {
        PipelineMetadataVk* vk() const { return (PipelineMetadataVk*)ptr; }
        void* ptr{};
    } md;
};

struct Geometry
{
    auto operator<=>(const Geometry& a) const { return meshlet_range <=> a.meshlet_range; }
    auto operator==(const Geometry& a) const { return meshlet_range == a.meshlet_range; }
    Range32u meshlet_range{}; // position inside meshlet buffer
    Handle<Buffer> blas_buffer{};
    struct Metadata
    {
        GeometryMetadataVk* vk() const { return (GeometryMetadataVk*)ptr; }
        void* ptr{};
    } md{};
};

struct ShaderEffect
{
    auto operator<=>(const ShaderEffect&) const = default;
    Handle<Pipeline> pipeline;
};

struct MeshPass
{
    using Effects = std::array<Handle<ShaderEffect>, (uint32_t)RenderPassType::LAST_ENUM>;
    static MeshPass init(std::string_view name,
                         const std::array<Handle<ShaderEffect>, (uint32_t)RenderPassType::LAST_ENUM>& effects = {})
    {
        MeshPass pass{};
        pass.name = name;
        pass.effects = effects;
        return pass;
    }
    bool operator==(const MeshPass& o) const { return name == o.name; }
    std::string name;
    Effects effects;
};

struct Material
{
    static Material init(std::string_view name, Handle<MeshPass> mesh_pass = {})
    {
        Material mat{};
        mat.name = name;
        mat.mesh_pass = mesh_pass;
        return mat;
    }
    auto operator<=>(const Material& t) const = default;
    StackString<64> name;
    Handle<MeshPass> mesh_pass;
    ImageView base_color_texture;         // todo: put those in an array?
    ImageView normal_texture;             // todo: put those in an array?
    ImageView metallic_roughness_texture; // todo: put those in an array?
};

struct Mesh
{
    auto operator<=>(const Mesh&) const = default;
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct Buffer
{
    static Buffer init(size_t capacity, Flags<BufferUsage> usage)
    {
        return Buffer{ .usage = usage, .capacity = capacity, .size = 0ull, .memory = {}, .md = {} };
    }

    Flags<BufferUsage> usage{};
    size_t capacity{};
    size_t size{};
    void* memory{};
    struct Metadata
    {
        BufferMetadataVk* vk() const { return (BufferMetadataVk*)ptr; }
        void* ptr{};
    } md;
};

struct Image
{
    static Image init(uint32_t width, uint32_t height, ImageFormat format, Flags<ImageUsage> usage,
                      ImageLayout layout = ImageLayout::UNDEFINED, uint32_t mips = 1)
    {
        mips = mips == ~0u ? (uint32_t)(std::log2f((float)std::min(width, height)) + 1) : mips;
        return init(width, height, 0, format, usage, mips, 1, layout);
    }
    static Image init(uint32_t width, uint32_t height, uint32_t depth, ImageFormat format, Flags<ImageUsage> usage,
                      uint32_t mips = 1, uint32_t layers = 1, ImageLayout layout = ImageLayout::UNDEFINED)
    {
        return Image{
            .type = depth > 0    ? ImageType::TYPE_3D
                    : height > 0 ? ImageType::TYPE_2D
                                 : ImageType::TYPE_1D,
            .format = format,
            .width = std::max(width, 1u),
            .height = std::max(height, 1u),
            .depth = std::max(depth, 1u),
            .mips = mips == 0u ? (uint32_t)(std::log2f((float)std::min(width, height)) + 1) : mips,
            .layers = layers,
            .usage = usage,
            .layout = layout,
            .md = {},
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
        ImageMetadataVk* vk() const { return (ImageMetadataVk*)ptr; }
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
            .md = {},
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

} // namespace gfx
} // namespace eng

ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::VertexBinding, ENG_HASH(t.binding, t.stride, t.instanced));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::VertexAttribute, ENG_HASH(t.location, t.binding, t.format, t.offset));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::StencilState,
                    ENG_HASH(t.fail, t.pass, t.depth_fail, t.compare, t.compare_mask, t.write_mask, t.ref));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::BlendState,
                    ENG_HASH(t.enable, t.src_color_factor, t.dst_color_factor, t.color_op, t.src_alpha_factor,
                             t.dst_alpha_factor, t.alpha_op, t.r > 0 ? true : false, t.g > 0 ? true : false,
                             t.b > 0 ? true : false, t.a > 0 ? true : false));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo::AttachmentState,
                    ENG_HASH(t.count, t.depth_format, t.stencil_format, ENG_HASH_SPAN(t.color_formats.begin(), t.count),
                             ENG_HASH_SPAN(t.blend_states.begin(), t.count)));
ENG_DEFINE_STD_HASH(eng::gfx::Descriptor, ENG_HASH(t.type, t.slot, t.size, t.stages, t.immutable_samplers));
ENG_DEFINE_STD_HASH(eng::gfx::DescriptorLayout, ENG_HASH(ENG_HASH_SPAN(t.layout.begin(), t.layout.size())));

ENG_DEFINE_STD_HASH(eng::gfx::PipelineCreateInfo,
                    ENG_HASH(t.layout, t.attachments, t.depth_test, t.depth_write, t.depth_compare, t.stencil_test,
                             t.stencil_front, t.stencil_back, t.topology, t.polygon_mode, t.culling, t.front_is_ccw,
                             t.line_width, ENG_HASH_SPAN(t.shaders.begin(), t.shaders.size()),
                             ENG_HASH_SPAN(t.bindings.begin(), t.bindings.size()),
                             ENG_HASH_SPAN(t.attributes.begin(), t.attributes.size())));
ENG_DEFINE_STD_HASH(eng::gfx::PipelineLayout,
                    ENG_HASH(t.push_range.stages, t.push_range.size, ENG_HASH_SPAN(t.layout.begin(), t.layout.size())));
ENG_DEFINE_STD_HASH(eng::gfx::Pipeline, ENG_HASH(t.info));
ENG_DEFINE_STD_HASH(eng::gfx::Shader, ENG_HASH(t.path));
ENG_DEFINE_STD_HASH(eng::gfx::Geometry, ENG_HASH(t.meshlet_range));
ENG_DEFINE_STD_HASH(eng::gfx::Material, ENG_HASH(t.mesh_pass, t.base_color_texture));
ENG_DEFINE_STD_HASH(eng::gfx::Mesh, ENG_HASH(t.geometry, t.material));
ENG_DEFINE_STD_HASH(eng::gfx::MeshPass, ENG_HASH(t.name));
ENG_DEFINE_STD_HASH(eng::gfx::ShaderEffect, ENG_HASH(t.pipeline));
// ENG_DEFINE_STD_HASH(eng::gfx::SamplerDescriptor,
//                     eng::hash::combine_fnv1a(t.filtering[0], t.filtering[1], t.addressing[0], t.addressing[1], t.addressing[2],
//                                              t.mip_lod[0], t.mip_lod[1], t.mip_lod[2], t.mipmap_mode, t.reduction_mode));
ENG_DEFINE_STD_HASH(eng::gfx::Sampler,
                    ENG_HASH(t.filtering.min, t.filtering.mag, t.addressing.u, t.addressing.v, t.addressing.w,
                             t.mip_blending, t.reduction_mode, t.lod.min, t.lod.max, t.lod.bias));
//  ENG_DEFINE_STD_HASH(eng::gfx::Texture, eng::hash::combine_fnv1a(t.view, t.layout, t.is_storage));
ENG_DEFINE_STD_HASH(eng::gfx::RendererMemoryRequirements, ENG_HASH(t.size, t.alignment, t.backend_data));

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

struct RenderResources
{
    RGResourceId constants;
    RGResourceId zpdepth;
    RGResourceId normal;
    RGResourceId ao;
    RGResourceId color;
};

struct TimestampQuery
{
    static float to_ms(const TimestampQuery& q);
    QueryPool* pool{};
    uint32_t index{}; // index and index+1 for diff
    StackString<64> label;
};

struct ScopedTimestampQuery
{
    ScopedTimestampQuery(std::string_view label, ICommandBuffer* cmd);
    ~ScopedTimestampQuery();
    TimestampQuery* query{};
    ICommandBuffer* cmd{};
};

// Used for adding passes to the render graph.
// Essentially divides vector of render passes into segments
// or partitions, and using the constants from this namespace
// adds render pass to the end of the corresponding segment.
// This solves a problem of having to coordinate add everything
// from one segment BEFORE adding anything ordered after.
namespace RenderOrder
{
inline constexpr uint32_t SETUP_TARGETS = 0;
inline constexpr uint32_t Z_PREPASS = 50;
inline constexpr uint32_t POST_Z = 51;
inline constexpr uint32_t MESH_RENDER = 100;
inline constexpr uint32_t UI = 150;
inline constexpr uint32_t PRESENT = 200;
}; // namespace RenderOrder

class Renderer
{
  public:
    constexpr static inline uint32_t frame_delay = 2;

    struct FrameData
    {
        ICommandPool* cmdpool{};
        Sync* acq_sem{};
        Sync* swp_sem{};
        Sync* ren_fen{};

        QueryPool* timestamp_pool;
        std::deque<TimestampQuery> timestamp_queries;
        std::deque<TimestampQuery> available_queries;

        RenderResources render_resources;

        struct RetiredResource
        {
            std::variant<Handle<Buffer>, Handle<Image>> resource;
            size_t deleted_at_frame{};
        };
        std::mutex retired_mutex;
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

        IndexFormat index_type{ IndexFormat::U16 };
        size_t vertex_count{};
        size_t index_count{};
        // size_t transform_count{};
        // size_t light_count{};

        Handle<Buffer> geom_blas; // todo: it's one big blas buffer for all the geometries that are forced to have blas built.
        // Handle<Buffer> geom_scratch; // todo: it's one big blas buffer for all the geometries that are forced to have blas built.

        Handle<Buffer> geom_tlas_buffer;
        TopAccelerationStructure geom_tlas;
    };

    struct GraphicsSettings
    {
        AOMode ao_mode{ AOMode::SSAO };
    };

    struct Settings
    {
        ImageFormat color_format{ ImageFormat::R16FG16FB16FA16F };
        ImageFormat depth_format{ ImageFormat::D32_SFLOAT };
        Vec2f new_render_resolution{};
        Vec2f render_resolution{};
        Vec2f present_resolution{};

        DepthCompare read_depth_compare{ DepthCompare::GEQUAL };
        DepthCompare rw_depth_compare{ DepthCompare::GEQUAL };

        Handle<PipelineLayout> common_layout;
        Handle<Pipeline> default_z_prepass_pipeline;
        Handle<Pipeline> default_forward_pipeline;
        Handle<Pipeline> apply_ao_pipeline;
        Handle<MeshPass> default_meshpass;
        Handle<Material> default_material;
        Handle<Image> default_base_color;

        GraphicsSettings gfx_settings;

        bool regenerate_swapchain{};
    };

    struct Passes
    {
        std::shared_ptr<pass::Pass> reconstruct_normals;
        std::array<std::shared_ptr<pass::Pass>, (int)AOMode::LAST_ENUM> ao{};
        std::array<std::shared_ptr<pass::Pass>, (int)RenderPassType::LAST_ENUM> mesh_passes{};
    };

    struct PagedAllocation
    {
        size_t page{ ~0ull };
        size_t start{ ~0ull };
        size_t elems{};
    };

    // todo: move this to some other file.
    // allocates floor(PAGE_SIZE_BYTES / sizeof(T)) pages of memory
    // and pushes element ranges into it. if range is bigger than the page,
    // it is split.
    // intention is to use it as a paginated linear temp allocator for batching big chunks of memory
    template <typename T, size_t PAGE_SIZE_BYTES> struct PageAllocator
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= PAGE_SIZE_BYTES, "Page cannot be smaller than the size of the element");
        inline static constexpr size_t PAGE_NUM_ELEMS = PAGE_SIZE_BYTES / sizeof(T);
        inline static constexpr size_t MAX_PAGES = ~uint32_t{}; // from all zeros to all_bits-1
        using Page = T*;
        struct PageMetadata
        {
            // returns free_space in free elems till the end
            size_t get_free_space() const { return PAGE_NUM_ELEMS - start; }
            size_t start{}; // in elems
        };

        ~PageAllocator()
        {
            current_page_md = {};
            std::allocator<T> alloc;
            for(auto* page : pages)
            {
                alloc.deallocate(page, PAGE_NUM_ELEMS);
            }
            pages = {};
            allocations = {};
        }

        void iterate_data_ranges(PagedAllocation* alloc, const auto& callback)
        {
            if(!alloc || alloc->page == ~0ull) { return; }
            size_t elems_left = alloc->elems;
            size_t page = alloc->page;
            size_t elems_start = alloc->start;
            while(elems_left > 0 && page < pages.size())
            {
                size_t elems_till_end = PAGE_NUM_ELEMS - elems_start;
                size_t elems_to_process = std::min(elems_left, elems_till_end);
                T* data_ptr = pages[page] + elems_start;
                callback(std::span<T>(data_ptr, elems_to_process));
                elems_left -= elems_to_process;
                ++page;
                elems_start = 0;
            }
        }

        void copy_to_linear_storage(PagedAllocation* alloc, std::span<std::byte> dst)
        {
            if(!alloc || alloc->page == ~0ull) { return; }
            size_t elems_left = std::min(alloc->elems, dst.size() / sizeof(T));
            size_t elems_processed = 0;
            size_t page = alloc->page;
            size_t elems_start = alloc->start;
            while(elems_left > 0 && page < pages.size())
            {
                const auto elems_till_end = PAGE_NUM_ELEMS - elems_start;
                const auto elems_to_process = std::min(elems_left, elems_till_end);
                T* const data_ptr = pages[page] + elems_start;
                memcpy(dst.data() + elems_processed * sizeof(T), data_ptr, elems_to_process * sizeof(T));
                elems_left -= elems_to_process;
                elems_processed += elems_to_process;
                ++page;
                elems_start = 0;
            }
        }

        PagedAllocation* insert(std::span<const T> src_data)
        {
            if(src_data.empty()) { return nullptr; }

            if(pages.empty() && !allocate_page()) { return nullptr; }

            size_t elems_left = src_data.size();
            auto free_space = current_page_md.get_free_space();

            auto& alloc = allocations.emplace_back();
            alloc.page = (uint32_t)(free_space > 0 ? pages.size() - 1 : pages.size());
            alloc.start = free_space > 0 ? current_page_md.start : 0;
            alloc.elems = src_data.size();

            while(elems_left > 0)
            {
                free_space = current_page_md.get_free_space();
                if(free_space == 0)
                {
                    if(!allocate_page())
                    {
                        allocations.pop_back();
                        return 0;
                    }
                    continue;
                }

                const auto elems_to_copy = std::min(elems_left, free_space);
                memcpy(pages.back() + current_page_md.start, src_data.data() + (src_data.size() - elems_left),
                       elems_to_copy * sizeof(T));
                current_page_md.start += elems_to_copy;
                elems_left -= elems_to_copy;
            }

            return &alloc;
        }

        bool allocate_page()
        {
            current_page_md = { ~0ull };
            if(pages.size() == MAX_PAGES) { return false; }
            std::allocator<T> alloc;
            auto* ptr = alloc.allocate(PAGE_NUM_ELEMS);
            if(!ptr) { return false; }
            pages.push_back(ptr);
            current_page_md = { 0ull };
            return true;
        }

        PageMetadata current_page_md{};
        std::vector<Page> pages;
        std::deque<PagedAllocation> allocations;
    };

    struct BuildGeometryBatch
    {
        Handle<Geometry> geom;
        Flags<GeometryFlags> flags;
        Flags<VertexComponent> vertex_layout;
        IndexFormat index_format{};
        PagedAllocation* position_range{};  // range, in floats, for positions in the context
        PagedAllocation* attribute_range{}; // range, in floats, for attributes in the context
        PagedAllocation* index_range{};     // range, in bytes, for indices in the context
        PagedAllocation* meshlet_range{};   // range for meshlets in the context
        ParsedGeometryReadySignal* geom_ready_signal{};
    };

    struct BuildGeometryContext
    {
        void add_descriptor(Handle<Geometry> geometry, const GeometryDescriptor& desc);
        PageAllocator<float, 16 * 1024 * 1024> positions;
        PageAllocator<float, 16 * 1024 * 1024> attributes;
        PageAllocator<std::byte, 16 * 1024 * 1024> indices;
        PageAllocator<Meshlet, 16 * 1024 * 1024> meshlets;
        std::vector<BuildGeometryBatch> batches;
    };

    void init(IRendererBackend* backend);
    void init_helper_geom();
    void init_pipelines();
    void init_perframes();
    void init_bufs();

    void update();
    void compile_rendergraph();
    void render_debug(const DebugGeometry& geom);

    // Creates buffer with optional gpu memory allocation. Thread-safe.
    Handle<Buffer> make_buffer(std::string_view name, Buffer&& buffer, AllocateMemory allocate = AllocateMemory::YES);
    // Enqueue resource to be destroyed in frame_delay frames, returning handle to the pool. Thread-safe.
    void queue_destroy(Handle<Buffer>& handle);
    // Creates image with optional gpu memory allocation. Thread-safe.
    Handle<Image> make_image(std::string_view name, Image&& image, AllocateMemory allocate = AllocateMemory::YES,
                             void* user_data = nullptr);
    // Enqueue resource to be destroyed in frame_delay frames, returning handle to the pool. Thread-safe.
    void queue_destroy(Handle<Image>& image, bool destroy_now = false);
    Handle<Sampler> make_sampler(Sampler&& sampler);
    Handle<Shader> make_shader(const std::filesystem::path& path);
    Handle<DescriptorLayout> make_layout(const DescriptorLayout& info);
    Handle<PipelineLayout> make_layout(const PipelineLayout& info);
    Handle<Pipeline> make_pipeline(const PipelineCreateInfo& info, Compilation compilation = Compilation::DEFERRED);
    void destroy_pipeline(Handle<Pipeline> pipeline);
    Sync* make_sync(const SyncCreateInfo& info);
    void destroy_sync(Sync* sync);
    Handle<Material> make_material(const Material& info);
    Handle<Geometry> make_geometry(const GeometryDescriptor& info);
    void build_pending_geometries();
    void meshletize_geometry(const BuildGeometryBatch& batch, std::vector<float>& out_positions, std::vector<float>& out_attributes,
                             std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets);
    Handle<Mesh> make_mesh(const MeshDescriptor& info);
    void make_blas(Handle<Geometry> geom);
    Handle<ShaderEffect> make_shader_effect(const ShaderEffect& info);
    Handle<MeshPass> make_mesh_pass(const MeshPass& info);
    // Handle<MeshPass> find_mesh_pass(std::string_view name);

    void resize_buffer(Handle<Buffer>& handle, size_t new_size, bool copy_data);
    void resize_buffer(Handle<Buffer>& handle, size_t upload_size, size_t offset, bool copy_data);
    void compile_pipeline(Pipeline& p);
    // void instance_blas(const BLASInstanceSettings& settingsi);
    // void update_transform(ecs::entity_id entity);

    SubmitQueue* get_queue(QueueType type);

    SubmitQueue* gq{};
    Swapchain* swapchain{};
    IRendererBackend* backend{};
    StagingBuffer* staging{};
    Settings settings;
    RGRenderGraph* rgraph{};

    Slotmap<Buffer> buffers;
    Slotmap<Image> images;

    HandleFlatSet<Sampler> samplers;
    std::vector<Shader> shaders;
    std::unordered_map<Handle<Shader>, std::vector<Handle<Pipeline>>> shader_usage_pipeline_map;
    Handle<fs::DirectoryListener> new_shaders_listener;

    std::vector<DescriptorLayout> dlayouts;
    std::vector<PipelineLayout> pplayouts;
    std::vector<Pipeline> pipelines;

    std::vector<Geometry> geometries;
    std::vector<Meshlet> meshlets;
    HandleFlatSet<Material> materials;
    std::vector<Mesh> meshes;
    HandleFlatSet<ShaderEffect> shader_effects;
    HandleFlatSet<MeshPass> mesh_passes;
    std::array<MeshRenderData, (int)RenderPassType::LAST_ENUM> mesh_render_data;

    BuildGeometryContext new_geometries;
    std::vector<Handle<Geometry>> new_blases;
    std::vector<Handle<Shader>> new_shaders;
    std::vector<Handle<Pipeline>> new_pipelines;
    std::vector<Handle<Material>> new_materials;
    std::vector<ecs::EntityId> new_transforms;
    std::vector<ecs::EntityId> new_lights;

    GeometryBuffers bufs;
    DebugGeomBuffers debug_bufs;
    SlotAllocator<uint32_t> gpu_resource_allocator;
    SlotAllocator<uint32_t> gpu_light_allocator;
    IDescriptorSetAllocator* descriptor_allocator{};
    ImGuiRenderer* imgui_renderer{};
    FrameData frame_datas[2]{};
    FrameData* current_data{};
    uint64_t current_frame{}; // monotonically increasing counter
    Passes passes;
};

} // namespace gfx

// clang-format off
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Buffer, { return &::eng::gfx::get_renderer().buffers.at(Slotmap<eng::gfx::Buffer, 1024>::SlotId{*handle}); });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Image, { return &::eng::gfx::get_renderer().images.at(Slotmap<eng::gfx::Image, 1024>::SlotId{*handle}); });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Geometry, { return &::eng::gfx::get_renderer().geometries[*handle]; });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Mesh, { return &::eng::gfx::get_renderer().meshes[*handle]; });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Shader, { return &::eng::gfx::get_renderer().shaders[*handle]; });
ENG_DEFINE_HANDLE_ALL_GETTERS(eng::gfx::Pipeline, { return &::eng::gfx::get_renderer().pipelines[*handle]; });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Sampler, { return &::eng::gfx::get_renderer().samplers.at(handle); });
//ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::BufferView, { return &::eng::gfx::get_renderer().buffer_views.at(handle); });
//ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::ImageView, { return &::eng::gfx::get_renderer().image_views.at(handle); });
//ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Texture);
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::Material, { return &::eng::gfx::get_renderer().materials.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::DescriptorLayout, { return &::eng::gfx::get_renderer().dlayouts[*handle]; });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::PipelineLayout, { return &::eng::gfx::get_renderer().pplayouts[*handle]; });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::MeshPass, { return &::eng::gfx::get_renderer().mesh_passes.at(handle); });
ENG_DEFINE_HANDLE_CONST_GETTERS(eng::gfx::ShaderEffect, { return &::eng::gfx::get_renderer().shader_effects.at(handle); });
// clang-format on

} // namespace eng
