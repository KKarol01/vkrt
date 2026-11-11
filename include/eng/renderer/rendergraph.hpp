#pragma once

#include <vector>
#include <span>
#include <string_view>
#include <memory>
#include <variant>
#include <eng/common/callback.hpp>
#include <eng/common/hash.hpp>
#include <eng/string/stack_string.hpp>
#include <eng/renderer/renderer_fwd.hpp>

namespace eng
{
namespace gfx
{

struct RGDebugData;
class GPUTransientAllocator;

// Render graph resource. Valid during only the frame it was created in.
template <typename T> struct RGHandle
{
    bool operator==(const RGHandle&) const = default;
    explicit operator bool() const { return index != ~0u; }
    u32 index{ ~0u }; // into RGRenderGraph::resources
};
using RGImage = RGHandle<Image>;
using RGBuffer = RGHandle<Buffer>;

struct RGPass
{
    using PassId = TypedId<RGPass, u64>;
    using PassOrder = u32;
    enum class Type
    {
        NONE,
        GRAPHICS,
        COMPUTE,
    };
    RGPass() = default;
    RGPass(const char* name, Type type) : id(ENG_HASH(name)), name(name), type(type) {}
    virtual ~RGPass() = default;
    bool is_graphics() const { return type == Type::GRAPHICS; }
    bool is_compute() const { return type == Type::COMPUTE; }
    virtual void execute(RGBuilder& pb) = 0;
    virtual void* get_user_data() const { return nullptr; }
    PassId id; // hash of name
    StackString<64> name;
    Type type{ Type::NONE };
    std::vector<u32> accesses; // indices into RGRenderGraph::accesses, in the order they were added
    ICommandBuffer* cmd{};     // if not null, needs to be executed
    TimestampQuery* query{};
};

template <typename UserType, typename ExecFunc> struct RGUserPass : public RGPass
{
    RGUserPass(const char* name, Type type, const ExecFunc& exec_func) : RGPass(name, type), exec_func(exec_func) {}
    ~RGUserPass() override = default;
    void execute(RGBuilder& pb) override { exec_func(pb, user_data); };
    void* get_user_data() const override { return (void*)&user_data; }
    ExecFunc exec_func{};
    UserType user_data{};
};

struct RGClear
{
    struct DepthStencil
    {
        float depth;
        std::optional<u32> stencil;
    };
    struct Color
    {
        glm::vec4 color;
    };
    static RGClear color(std::array<float, 4> color = { 0.0f, 0.0f, 0.0f, 1.0f })
    {
        return RGClear{ Color{ glm::vec4{ color[0], color[1], color[2], color[3] } } };
    }
    static RGClear depth_stencil(float depth, std::optional<u32> stencil = {})
    {
        return RGClear{ DepthStencil{ depth, stencil } };
    }
    bool is_color() const { return value.index() == 0; }
    Color get_color() const { return std::get<0>(value); }
    DepthStencil get_ds() const { return std::get<1>(value); }
    std::variant<Color, DepthStencil> value;
};

// Synchronization state of a whole resource (all mips and layers of an image share one state).
struct RGResourceState
{
    ImageLayout layout{ ImageLayout::UNDEFINED };
    Flags<PipelineStage> write_stages{};  // stages of the last write or layout transition
    Flags<PipelineAccess> write_access{}; // memory writes of the last write that still need to be made available
    Flags<PipelineStage> read_stages{};   // stages that read since the last write or transition
    Flags<PipelineStage> read_stages_accum{}; // stores read stage flag bits in a chain of read operations to avoid inserting the same barrier twice; reset on write
    Flags<PipelineAccess> read_accesses_accum{}; // stores read access flag bits in a chain of read operations to avoid inserting the same barrier twice; reset on write
};

struct RGResource
{
    using NativeResource = std::variant<Handle<Buffer>, Handle<Image>>;
    RGResource(std::string_view name, const NativeResource& native, bool is_persistent)
        : name(name), native(native), is_persistent(is_persistent),
          name_hash(is_persistent && !name.empty() ? ENG_HASH(name) : 0)
    {
    }

    // Handle to u64 key. Highest bit set if is Image.
    static u64 make_key(const NativeResource& native)
    {
        if(native.index() == 0) { return (u64)*std::get<0>(native); }
        return (1ull << 63) | (u64)*std::get<1>(native);
    }
    bool is_buffer() const { return native.index() == 0; }
    bool is_aliased() const { return !is_persistent; }
    Handle<Buffer> as_buffer() const { return std::get<0>(native); }
    Handle<Image> as_image() const { return std::get<1>(native); }
    StackString<64> name;
    NativeResource native{};
    bool is_persistent{};   // state is kept between frames (imported, or created with persistent())
    u64 name_hash{};        // name hash if created with persistent()
    void* aliased_memory{}; // memory from the aliased pool
    u32 first_pass{ ~0u };  // execution-order lifetime (index into passes)
    u32 last_pass{ ~0u };
    RGResourceState state{}; // tracked while executing
};

struct RGAccess
{
    bool is_read() const { return (access & PipelineAccess::READS).any(); }
    bool is_write() const { return (access & PipelineAccess::WRITES).any(); }
    u32 resource{ ~0u }; // index into RGRenderGraph::resources
    bool is_first{};     // added the resource to the graph; a no-op unless it clears
    union {
        BufferView buffer_view; // if the resource is a buffer
        ImageView image_view;
    };
    ImageLayout layout{ ImageLayout::UNDEFINED };
    Flags<PipelineStage> stage{};
    Flags<PipelineAccess> access{};
    std::optional<RGClear> clear; // images only: discard the contents and clear, before the pass runs
};

// Resources created with persistent(): `history` copies, rotated each frame, looked up by name.
struct RGPersistentResource
{
    u64 hash{};                                     // hash of the description; the copies are recreated when it changes
    std::vector<RGResource::NativeResource> copies; // copy used in a frame: copies[RGRenderGraph::frame % copies.size()]
};

struct RGBuilder
{
    // Import external resource. Safe to call more than once on the same resource during the same frame.
    RGImage import_resource(Handle<Image> image, const std::optional<RGClear>& clear = {});
    // Import external resource. Safe to call more than once on the same resource during the same frame.
    RGBuffer import_resource(Handle<Buffer> buffer);

    // Creates a resource that only lives during this frame (its memory may be aliased with other transient ones).
    RGImage create_resource(std::string_view name, Image&& desc, const std::optional<RGClear>& clear = {});
    // Creates a resource that only lives during this frame (its memory may be aliased with other transient ones).
    RGBuffer create_resource(std::string_view name, Buffer&& desc);

    // Creates a resource, but only once, for, say, double-buffering purposes. Rotates resource each frame and wraps after history render frames.
    RGImage persistent(std::string_view name, Image&& desc, const std::optional<RGClear>& clear = {}, u32 history = 1);
    // Creates a resource, but only once, for, say, double-buffering purposes. Rotates resource each frame and wraps after history render frames.
    RGBuffer persistent(std::string_view name, Buffer&& desc, u32 history = 1);

    // The copy of a persistent() resource used in the previous frame.
    RGImage previous(RGImage image);
    // The copy of a persistent() resource used in the previous frame.
    RGBuffer previous(RGBuffer buffer);

    RGImage access_resource(RGImage image, ImageLayout layout, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                            std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                            Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u });
    RGBuffer access_resource(RGBuffer buffer, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                             Range64u range = { 0ull, ~0ull });

    RGImage sample_texture(RGImage image, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                           Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        return access_resource(image, ImageLayout::READ_ONLY, shader_stage(), PipelineAccess::SHADER_READ_BIT, format,
                               type, mips, layers);
    }

    RGImage access_depth(RGImage image, std::optional<ImageFormat> format = {})
    {
        const auto stage = PipelineStage::EARLY_Z_BIT | PipelineStage::LATE_Z_BIT;
        return access_resource(image, ImageLayout::ATTACHMENT, stage, PipelineAccess::DS_RW, format, ImageViewType::TYPE_2D);
    }

    RGImage access_color(RGImage image, std::optional<ImageFormat> format = {})
    {
        return access_resource(image, ImageLayout::ATTACHMENT, PipelineStage::COLOR_OUT_BIT,
                               PipelineAccess::COLOR_RW_BIT, format, ImageViewType::TYPE_2D);
    }

    RGImage read_image(RGImage image, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                       Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        return access_resource(image, ImageLayout::GENERAL, shader_stage(), PipelineAccess::STORAGE_READ_BIT, format,
                               type, mips, layers);
    }

    RGImage write_image(RGImage image, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                        Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        return access_resource(image, ImageLayout::GENERAL, shader_stage(), PipelineAccess::STORAGE_WRITE_BIT, format,
                               type, mips, layers);
    }

    RGImage read_write_image(RGImage image, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                             Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        return access_resource(image, ImageLayout::GENERAL, shader_stage(), PipelineAccess::STORAGE_RW, format, type, mips, layers);
    }

    RGBuffer read_buffer(RGBuffer buffer, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::VERTEX_BIT | PipelineStage::FRAGMENT_BIT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        return access_resource(buffer, stage, PipelineAccess::STORAGE_READ_BIT, range);
    }

    RGBuffer write_buffer(RGBuffer buffer, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::ALL
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        return access_resource(buffer, stage, PipelineAccess::STORAGE_RW, range);
    }

    RGBuffer read_write_buffer(RGBuffer buffer, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::ALL
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        return access_resource(buffer, stage, PipelineAccess::STORAGE_RW, range);
    }

    RGImage copy_source(RGImage image)
    {
        return access_resource(image, ImageLayout::TRANSFER_SRC, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
    }

    RGImage copy_dest(RGImage image)
    {
        return access_resource(image, ImageLayout::TRANSFER_DST, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT);
    }

    RGBuffer read_index(RGBuffer buffer)
    {
        ENG_ASSERT(pass->is_graphics());
        return access_resource(buffer, PipelineStage::VERTEX_INPUT_BIT, PipelineAccess::INDIRECT_READ_BIT);
    }

    RGBuffer read_indirect(RGBuffer buffer)
    {
        ENG_ASSERT(pass->is_graphics());
        return access_resource(buffer, PipelineStage::INDIRECT_BIT, PipelineAccess::INDIRECT_READ_BIT);
    }

    // This pass' access to the resource: the last one, or the nth one if the pass accesses it more than once.
    const RGAccess& get_acc(RGImage image, u32 nth = ~0u) const;
    const RGAccess& get_acc(RGBuffer buffer, u32 nth = ~0u) const;
    const ImageView& get_img(RGImage image, u32 nth = ~0u) const { return get_acc(image, nth).image_view; }
    const BufferView& get_buf(RGBuffer buffer, u32 nth = ~0u) const { return get_acc(buffer, nth).buffer_view; }

    ICommandBuffer* open_cmd_buf();

    RGPass* pass{};
    RGRenderGraph* graph{};

  private:
    Flags<PipelineStage> shader_stage() const
    {
        return pass->is_graphics()  ? PipelineStage::FRAGMENT_BIT
               : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                    : PipelineStage::NONE;
    }
    u32 add_resource(const RGResource& resource, const std::optional<RGClear>& clear = {});
    u32 import_native(const RGResource::NativeResource& native, const std::optional<RGClear>& clear);
    u32 add_access(const RGAccess& a);
    const RGAccess& get_pass_acc(u32 resource, u32 nth) const;
};

class RGRenderGraph
{
  public:
    struct OrderedPass
    {
        bool operator<(const OrderedPass& a) const { return order < a.order; }
        std::unique_ptr<RGPass> pass;
        RGPass::PassOrder order{};
    };

    // utility funcs for easy access to resources
    RGAccess& get_acc(u32 access) { return accesses[access]; }
    RGResource& get_res(u32 resource) { return resources[resource]; }
    template <typename T> RGResource& get_res(RGHandle<T> h) { return resources[h.index]; }
    Handle<Buffer> get_buf(RGBuffer h) { return get_res(h).as_buffer(); }
    Handle<Image> get_img(RGImage h) { return get_res(h).as_image(); }

    void init(Renderer* r);

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_pass(const char* name, RGPass::PassOrder order, RGPass::Type type, const SetupFunc& setup_func,
                             const ExecFunc& exec_func)
    {
        OrderedPass op{};
        op.pass = std::make_unique<RGUserPass<UserType, ExecFunc>>(name, type, exec_func);
        op.order = order;

        if(namedpasses.contains(op.pass->id))
        {
            ENG_ERROR("Pass \"{}\" was already defined.", name);
            static UserType null_object{};
            null_object = UserType{}; // a previous caller may have written to it
            return null_object;
        }

        auto it = std::upper_bound(passes.begin(), passes.end(), op);
        it = passes.insert(it, std::move(op));

        namedpasses[it->pass->id] = &*it->pass;

        RGBuilder pb{ &*it->pass, this };
        auto* user_data = static_cast<UserType*>(it->pass->get_user_data());
        std::invoke(setup_func, pb, *user_data);
        return *user_data;
    }

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_graphics_pass(const char* name, RGPass::PassOrder order, const SetupFunc& setup_func, const ExecFunc& exec_func)
    {
        return add_pass<UserType>(name, order, RGPass::Type::GRAPHICS, setup_func, exec_func);
    }

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_compute_pass(const char* name, RGPass::PassOrder order, const SetupFunc& setup_func, const ExecFunc& exec_func)
    {
        return add_pass<UserType>(name, order, RGPass::Type::COMPUTE, setup_func, exec_func);
    }

    void compile();

    Sync* execute(Sync** wait_syncs = nullptr, u32 wait_count = 0);

    void free_resource(RGResource& res);

    SubmitQueue* queue{};
    ICommandPool* cmd_pools[2]{};
    Sync* sems[2]{};
    GPUTransientAllocator* allocators[2]{};
    GPUTransientAllocator* allocator{};
    RGDebugData* m_debug_datas_arr[2]{};

    u64 frame{};                                                        // incremented by execute()
    std::unordered_map<u64, RGPersistentResource> persistent_resources; // name hash -> resources from persistent()
    std::unordered_map<u64, RGResourceState> persistent_states_map; // RGResource::make_key() -> state after last use
    std::vector<RGResource> resources;
    std::vector<RGAccess> accesses;
    std::vector<OrderedPass> passes;
    std::vector<std::vector<u32>> pass_releases; // per pass: resources whose lifetime ends in it
    std::unordered_map<RGPass::PassId, RGPass*> namedpasses;
};

struct RGDebugData
{
    struct Resource
    {
        std::string name;
        bool persistent{};
        bool aliased_memory{};
    };
    struct Access
    {
        u32 resource{ ~0u };
        Flags<PipelineStage> stage{};
        Flags<PipelineAccess> access{};
        ImageLayout layout{};
        bool last_access{}; // lifetime ends in this pass (destroyed here, if !resource.persistent)
    };
    struct Pass
    {
        std::string name;
        std::vector<Access> accesses;
        TimestampQuery* query{};
    };

    void build(RGRenderGraph* rg);

    void clear()
    {
        resources.clear();
        passes.clear();
    }

    std::vector<Resource> resources;
    std::vector<Pass> passes; // in execution order
};

} // namespace gfx
} // namespace eng
