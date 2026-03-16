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

class GPUTransientAllocator;
using RGResourceId = Handle<RGResource>;
using RGAccessId = Handle<RGAccess>;

struct RGPass
{
    using PassId = TypedId<RGPass, uint64_t>;
    using PassOrder = uint32_t;
    enum class Type
    {
        NONE,
        GRAPHICS,
        COMPUTE,
    };
    RGPass() = default;
    RGPass(const char* name, Type type) : id(ENG_HASH_STR(name)), name(name), type(type) {}
    virtual ~RGPass() = default;
    bool is_graphics() const { return type == Type::GRAPHICS; }
    bool is_compute() const { return type == Type::COMPUTE; }
    virtual void execute(RGBuilder& pb) = 0;
    virtual void* get_user_data() const { return nullptr; }
    PassId id; // hash of name
    StackString<32> name;
    Type type{ Type::NONE };
    std::vector<RGAccessId> accesses;
    Flags<PipelineStage> stage_mask{}; // accumulated access stages for barrier/semaphore
    ICommandBuffer* cmd{};             // if not null, needs to be executed
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
        std::optional<uint32_t> stencil;
    };
    struct Color
    {
        glm::vec4 color;
    };
    static RGClear color(std::array<float, 4> color)
    {
        return RGClear{ Color{ glm::vec4{ color[0], color[1], color[2], color[3] } } };
    }
    static RGClear depth_stencil(float depth, std::optional<uint32_t> stencil = {})
    {
        return RGClear{ DepthStencil{ depth, stencil } };
    }
    bool is_color() const { return value.index() == 0; }
    Color get_color() const { return std::get<0>(value); }
    DepthStencil get_ds() const { return std::get<1>(value); }
    std::variant<Color, DepthStencil> value;
};

struct RGResource
{
    using NativeResource = std::variant<Handle<Buffer>, Handle<Image>>;
    RGResource(const char* name, const NativeResource& native, bool is_persistent, bool is_imported,
               const std::optional<RGClear>& clear = {})
        : name(name), native(native), is_persistent(is_persistent), is_imported(is_imported), clear(clear)
    {
    }
    bool is_buffer() const { return native.index() == 0; }
    Handle<Buffer> as_buffer() const { return std::get<0>(native); }
    Handle<Image> as_image() const { return std::get<1>(native); }
    StackString<64> name;
    NativeResource native;
    RGAccessId last_access;
    uint32_t last_read_group{ ~0u };
    uint32_t last_write_group{ ~0u };
    bool is_persistent{};
    bool is_imported{};
    void* alloc{}; // from transient allocator if not persistent
    std::optional<RGClear> clear;
};

struct RGAccess
{
    bool is_read() const { return (access & PipelineAccess::READS).any(); }
    bool is_write() const { return (access & PipelineAccess::WRITES).any(); }
    // note: this might prove to be problematic if some resources will actually need to use none/none
    bool is_first_access() const { return !prev_access; }
    RGResourceId resource;
    RGAccessId prev_access;
    union {
        BufferView buffer_view; // if resource->is_buffer() == true or layout == undefined
        ImageView image_view;
    };
    ImageLayout layout{ ImageLayout::UNDEFINED };
    Flags<PipelineStage> stage;
    Flags<PipelineAccess> access;
};

struct PersistentStorage
{
    RGResource::NativeResource native;
};

struct RGBuilder
{
    RGAccessId add_resource(const RGResource& resource, const std::optional<RGClear>& clear = {});
    RGAccessId import_resource(const RGResource::NativeResource& resource, const std::optional<RGClear>& clear = {});
    PersistentStorage* find_persistent(uint64_t namehash);
    RGAccessId create_resource(const char* name, Buffer&& a, bool persistent = false);
    RGAccessId create_resource(const char* name, Image&& a, bool persistent = false, const std::optional<RGClear>& clear = {});
    RGAccessId add_access(const RGAccess& a);
    RGAccessId access_resource(RGAccessId acc, ImageLayout layout, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                               std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                               Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u });
    RGAccessId access_resource(RGAccessId acc, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                               Range64u range = { 0ull, ~0ull });

    RGAccessId sample_texture(RGAccessId acc, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                              Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::SHADER_READ_BIT;
        const auto layout = ImageLayout::READ_ONLY;
        return access_resource(acc, layout, stage, access, format, type, mips, layers);
    }

    RGAccessId access_depth(RGAccessId acc, std::optional<ImageFormat> format = {})
    {
        const auto stage = PipelineStage::EARLY_Z_BIT | PipelineStage::LATE_Z_BIT;
        const auto access = PipelineAccess::DS_RW;
        const auto layout = ImageLayout::ATTACHMENT;
        return access_resource(acc, layout, stage, access, format, ImageViewType::TYPE_2D);
    }

    RGAccessId access_color(RGAccessId acc, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {})
    {
        const auto stage = PipelineStage::COLOR_OUT_BIT;
        const auto access = PipelineAccess::COLOR_RW_BIT;
        const auto layout = ImageLayout::ATTACHMENT;
        return access_resource(acc, layout, stage, access, format, ImageViewType::TYPE_2D);
    }

    RGAccessId read_image(RGAccessId acc, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                          Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_READ_BIT;
        const auto layout = ImageLayout::GENERAL;
        return access_resource(acc, layout, stage, access, format, type, mips, layers);
    }

    RGAccessId write_image(RGAccessId acc, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                           Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_WRITE_BIT;
        const auto layout = ImageLayout::GENERAL;
        return access_resource(acc, layout, stage, access, format, type, mips, layers);
    }

    RGAccessId read_write_image(RGAccessId acc, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                                Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_RW;
        const auto layout = ImageLayout::GENERAL;
        return access_resource(acc, layout, stage, access, format, type, mips, layers);
    }

    RGAccessId read_buffer(RGAccessId acc, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::VERTEX_BIT | PipelineStage::FRAGMENT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_READ_BIT;
        return access_resource(acc, stage, access, range);
    }

    RGAccessId write_buffer(RGAccessId acc, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::ALL
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_RW;
        return access_resource(acc, stage, access, range);
    }

    RGAccessId read_write_buffer(RGAccessId acc, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::ALL
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_RW;
        return access_resource(acc, stage, access, range);
    }

    ICommandBuffer* open_cmd_buf();

    RGPass* pass{};
    RGRenderGraph* graph{};
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

    struct ExecutionGroup
    {
        std::vector<RGPass*> passes;
    };

    // utility funcs for easy access to resources
    RGAccess& get_acc(RGAccessId a) { return accesses[*a]; }
    RGAccessId get_acc(RGResourceId a) { return get_res(a).last_access; }
    RGResource& get_res(RGResourceId a) { return resources[*a]; }
    RGResource& get_res(RGAccessId a) { return resources[*get_acc(a).resource]; }
    RGResourceId get_res_id(RGAccessId a) { return get_acc(a).resource; }
    Handle<Buffer> get_buf(RGAccessId a) { return get_res(a).as_buffer(); }
    Handle<Buffer> get_buf(RGResourceId a) { return get_res(a).as_buffer(); }
    Handle<Image> get_img(RGAccessId a) { return get_res(a).as_image(); }
    Handle<Image> get_img(RGResourceId a) { return get_res(a).as_image(); }

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
            return null_object;
        }

        auto it = std::upper_bound(passes.begin(), passes.end(), op);
        it = passes.insert(it, std::move(op));

        namedpasses[it->pass->id] = &*it->pass;

        RGBuilder pb{ &*it->pass, this };
        auto* user_data = static_cast<UserType*>(it->pass->get_user_data());
        setup_func(pb, *user_data);
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

    Sync* execute(Sync** wait_syncs = nullptr, uint32_t wait_count = 0);

    void free_resource(RGResource& res);

    SubmitQueue* queue{};
    ICommandPool* cmd_pools[2]{};
    Sync* sems[2]{};
    GPUTransientAllocator* allocator{};

    std::unordered_map<std::pair<RGPass::PassId, uint64_t>, PersistentStorage, hash::PairHash> persistent_resources;
    std::vector<RGResource> resources;
    std::vector<RGAccess> accesses;
    std::vector<OrderedPass> passes;
    std::vector<ExecutionGroup> groups;
    std::unordered_map<RGPass::PassId, RGPass*> namedpasses;
};

} // namespace gfx
} // namespace eng