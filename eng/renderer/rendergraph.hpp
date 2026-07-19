#pragma once

#include <vector>
#include <span>
#include <string_view>
#include <memory>
#include <variant>
#include <optional>
#include <eng/common/callback.hpp>
#include <eng/common/hash.hpp>
#include <eng/renderer/types.hpp>
#include <eng/string/stack_string.hpp>

namespace eng
{
namespace gfx
{

struct RGDebugData;
class GPUTransientAllocator;
struct RGPersistentResource;
using RGResourceId = TypedId<RGResource, u32>;
inline static constexpr u32 RGRESOURCEID_PERSISTENT_BIT = 0x80000000;
using RGAccessId = TypedId<RGAccess, u32>;
using RGNativeResourceVariant = std::variant<Buffer, Image>;

struct RGPass
{
    using PassId = TypedId<RGPass, u64>;
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
    std::map<RGResourceId, RGAccessId> res_to_acc;
    Flags<PipelineStage> stage_mask{}; // accumulated access stages for barrier/semaphore
    ICommandBuffer* cmd{};             // if not null, needs to be executed
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

struct RGResource
{
    using NativeResource = std::variant<Handle<Buffer>, Handle<Image>>;
    RGResource() = default;
    RGResource(std::string_view name, const NativeResource& native, RGPersistentResource* persistent, bool is_aliased,
               bool is_external = false, const std::optional<RGClear>& clear = {})
        : name(name), native(native), persistent(persistent), is_aliased(is_aliased), is_external(is_external), clear(clear)
    {
    }
    bool is_persistent() const { return persistent != nullptr; }
    bool is_buffer() const { return native.index() == 0; }
    Handle<Buffer> as_buffer() const { return std::get<0>(native); }
    Handle<Image> as_image() const { return std::get<1>(native); }
    StackString<64> name;
    NativeResource native{};
    RGAccessId last_access{};
    u32 last_read_group{ ~0u };
    u32 last_write_group{ ~0u };
    RGPersistentResource* persistent{};
    bool is_aliased{};
    bool is_external{}; // imported or created persistent
    void* alloc{};      // from transient allocator if not persistent
    std::optional<RGClear> clear;
};

struct RGWaitSync
{
    Sync* sync{};
    u64 wait_value{ ~0u };
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
    RGWaitSync* wait_sync{}; // optionally links to persistent storage if importing from previous frame which might not have yet finished
};

struct RGPersistentResource
{
    hash_t name_hash{};
    hash_t object_hash{};
    RGResourceId id{};
    RGResource resource;
    RGWaitSync wait_sync{};
    ImageLayout last_layout{ ImageLayout::UNDEFINED };
};

struct RGBuilder
{
    RGResourceId add_resource(const RGResource& resource, const std::optional<RGClear>& clear = {});
    RGResourceId import_resource(RGResourceId id, DiscardContents discard = DiscardContents::NO,
                                 const std::optional<RGClear>& clear = {});
    RGResourceId import_resource(const RGResource::NativeResource& resource,
                                 DiscardContents discard = DiscardContents::NO, const std::optional<RGClear>& clear = {});
    RGResourceId create_resource(std::string_view name, RGNativeResourceVariant&& a,
                                 const std::optional<RGClear>& clear = {}, bool is_persistent = false);
    RGResourceId create_resource(std::string_view name, Buffer&& a, bool is_persistent = false);
    RGResourceId create_resource(std::string_view name, Image&& a, const std::optional<RGClear>& clear = {},
                                 bool is_persistent = false);
    void add_access(const RGAccess& a);
    RGResourceId access_resource(RGResourceId acc, ImageLayout layout, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                                 std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                                 Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u });
    RGResourceId access_resource(RGResourceId acc, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                                 Range64u range = { 0ull, ~0ull });

    RGResourceId sample_texture(RGResourceId res, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                                Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT_BIT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::SHADER_READ_BIT;
        const auto layout = ImageLayout::READ_ONLY;
        return access_resource(res, layout, stage, access, format, type, mips, layers);
    }

    RGResourceId access_depth(RGResourceId res, std::optional<ImageFormat> format = {})
    {
        const auto stage = PipelineStage::EARLY_Z_BIT | PipelineStage::LATE_Z_BIT;
        const auto access = PipelineAccess::DS_RW;
        const auto layout = ImageLayout::ATTACHMENT;
        return access_resource(res, layout, stage, access, format, ImageViewType::TYPE_2D);
    }

    RGResourceId access_color(RGResourceId res, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {})
    {
        const auto stage = PipelineStage::COLOR_OUT_BIT;
        const auto access = PipelineAccess::COLOR_RW_BIT;
        const auto layout = ImageLayout::ATTACHMENT;
        return access_resource(res, layout, stage, access, format, ImageViewType::TYPE_2D);
    }

    RGResourceId read_image(RGResourceId res, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                            Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT_BIT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_READ_BIT;
        const auto layout = ImageLayout::GENERAL;
        return access_resource(res, layout, stage, access, format, type, mips, layers);
    }

    RGResourceId write_image(RGResourceId res, std::optional<ImageFormat> format = {}, std::optional<ImageViewType> type = {},
                             Range32u mips = { 0u, ~0u }, Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT_BIT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_WRITE_BIT;
        const auto layout = ImageLayout::GENERAL;
        return access_resource(res, layout, stage, access, format, type, mips, layers);
    }

    RGResourceId read_write_image(RGResourceId res, std::optional<ImageFormat> format = {},
                                  std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                  Range32u layers = { 0u, ~0u })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT_BIT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_RW;
        const auto layout = ImageLayout::GENERAL;
        return access_resource(res, layout, stage, access, format, type, mips, layers);
    }

    RGResourceId read_buffer(RGResourceId res, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::VERTEX_BIT | PipelineStage::FRAGMENT_BIT
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_READ_BIT;
        return access_resource(res, stage, access, range);
    }

    RGResourceId write_buffer(RGResourceId res, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::ALL
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_RW;
        return access_resource(res, stage, access, range);
    }

    RGResourceId read_write_buffer(RGResourceId res, Range64u range = { 0ull, ~0ull })
    {
        const auto stage = pass->is_graphics()  ? PipelineStage::ALL
                           : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                : PipelineStage::NONE;
        const auto access = PipelineAccess::STORAGE_RW;
        return access_resource(res, stage, access, range);
    }

    RGResourceId copy_source(RGResourceId res)
    {
        return access_resource(res, ImageLayout::TRANSFER_SRC, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_READ_BIT);
    }

    RGResourceId copy_dest(RGResourceId res)
    {
        return access_resource(res, ImageLayout::TRANSFER_DST, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT);
    }

    RGResourceId read_index(RGResourceId res)
    {
        ENG_ASSERT(pass->is_graphics());
        return access_resource(res, PipelineStage::VERTEX_INPUT_BIT, PipelineAccess::INDIRECT_READ_BIT);
    }

    RGResourceId read_indirect(RGResourceId res)
    {
        ENG_ASSERT(pass->is_graphics());
        return access_resource(res, PipelineStage::INDIRECT_BIT, PipelineAccess::INDIRECT_READ_BIT);
    }

    const RGAccess& get_acc(RGAccessId acc) const;
    const RGAccess& get_acc(RGResourceId res) const;
    const BufferView& get_buf(RGAccessId acc) const;
    const BufferView& get_buf(RGResourceId acc) const;
    const ImageView& get_img(RGAccessId acc) const;
    const ImageView& get_img(RGResourceId acc) const;

    ICommandBuffer* open_cmd_buf();

    RGPass* pass{};
    RGRenderGraph* graph{};
};

class RGRenderGraph
{
  public:
    struct ExecutionGroup
    {
        std::vector<RGPass*> passes;
    };

    // utility funcs for easy access to resources
    static constexpr bool is_persistent(RGResourceId id) { return id && (*id & RGRESOURCEID_PERSISTENT_BIT) != 0; }
    static constexpr u32 extract_idx(RGResourceId id) { return *id & ~RGRESOURCEID_PERSISTENT_BIT; }
    RGResource& get_res(RGResourceId a)
    {
        ENG_ASSERT(a);
        return is_persistent(a) ? persistent_resources[extract_idx(a)].resource : resources[extract_idx(a)];
    }
    RGResource& get_res(RGAccessId a) { return get_res(get_acc(a).resource); }
    RGAccess& get_acc(RGAccessId a) { return accesses[*a]; }
    RGAccess& get_acc(RGResourceId a) { return get_acc(get_res(a).last_access); }
    Handle<Buffer> get_buf(RGResourceId a) { return get_res(a).as_buffer(); }
    Handle<Image> get_img(RGResourceId a) { return get_res(a).as_image(); }

    void init(Renderer* r);

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_pass(const char* name, RGPass::Type type, const SetupFunc& setup_func, const ExecFunc& exec_func)
    {
        RGPass& pass = *passes.emplace_back(std::make_unique<RGUserPass<UserType, ExecFunc>>(name, type, exec_func));
        ENG_ASSERT(pass.id);

        RGBuilder pb{ &pass, this };
        auto* user_data = static_cast<UserType*>(pass.get_user_data());
        std::invoke(setup_func, pb, *user_data);
        return *user_data;
    }

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_graphics_pass(const char* name, const SetupFunc& setup_func, const ExecFunc& exec_func)
    {
        return add_pass<UserType>(name, RGPass::Type::GRAPHICS, setup_func, exec_func);
    }

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_compute_pass(const char* name, const SetupFunc& setup_func, const ExecFunc& exec_func)
    {
        return add_pass<UserType>(name, RGPass::Type::COMPUTE, setup_func, exec_func);
    }

    void compile();

    Sync* execute(Sync** wait_syncs = nullptr, u32 wait_count = 0);

    void queue_destroy_resource(RGResource& res);

    SubmitQueue* queue{};
    ICommandPool* cmd_pools[2]{};
    Sync* sems[2]{};
    GPUTransientAllocator* allocators[2]{};
    GPUTransientAllocator* allocator{};
    RGDebugData* m_debug_datas_arr[2]{};

    std::deque<RGPersistentResource> persistent_resources;
    std::vector<RGResource> resources;

    std::vector<RGAccess> accesses;
    std::vector<std::unique_ptr<RGPass>> passes;
    std::vector<ExecutionGroup> groups;
    // std::unordered_map<RGPass::PassId, RGPass*> namedpasses;

    bool passes_serialized{};
    bool memory_aliasing_disabled{};
};

struct RGDebugData
{
    struct Resource
    {
        std::string name;
        // std::variant<Buffer, Image> resource;
        bool persistent{};
        bool aliased_memory{};
    };
    struct Access
    {
        u32 resource{ ~0u };
        Flags<PipelineStage> stage{};
        Flags<PipelineAccess> access{};
        ImageLayout layout{};
        bool last_access{}; // is getting destroyed here, if !resource.persistent
    };
    struct Pass
    {
        std::string name;
        std::vector<Access> accesses;
        TimestampQuery* query{};
    };
    struct Group
    {
        std::vector<Pass> passes;
    };

    void build(RGRenderGraph* rg);

    void clear()
    {
        resources.clear();
        groups.clear();
    }

    std::vector<Resource> resources;
    std::vector<Group> groups;
};

} // namespace gfx
} // namespace eng