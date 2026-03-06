#pragma once

#include <vector>
#include <span>
#include <string_view>
#include <memory>
#include <variant>
#include <unordered_set>
#include <unordered_map>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/common/hash.hpp>
#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/string/stack_string.hpp>

namespace eng
{
namespace gfx
{

/*
 * Buddy-like gpu memory allocator for aliased resources, supporting frames-in-flight.
 * Allocates pages, and subdivides into pow2-sized buckets.
 * During free, checks if enough frames were rendered, so buddies can be merged.
 * Each frame, before first allocation, defrag happens.
 *
 * Call init() with a callback to some kind of gpu aliased allocator (for example vma)
 *
 * Right now no memory type checking happens and two pages cannot be glued into bigger one.
 * No memory budget either.
 */
class GPUTransientAllocator
{
    inline static constexpr size_t MIN_ALLOC = 64 * 1024;
    inline static constexpr size_t PAGE_SIZE = 128 * 1024 * 1024;
    inline static constexpr size_t PAGE_ALIGNMENT = 1024 * 1024;

    struct Bucket
    {
        bool operator<(const Bucket& b) const
        {
            return std::tie(size, page, page_offset) < std::tie(b.size, b.page, b.page_offset);
        }
        bool operator==(const Bucket& b) const { return !(*this < b) && !(b < *this); }
        bool is_free() const;
        bool can_be_merged_with_buddy(const Bucket& buddy) const;
        uint32_t page{ ~0u };
        size_t page_offset{};
        size_t size{};
        size_t freed_at{ ~0ull }; // frame index;
    };

  public:
    void init(auto&& alias_allocator)
    {
        ENG_ASSERT(pages.empty());
        *this = {};
        allocator = alias_allocator;
    }
    void* allocate(const RendererMemoryRequirements& reqs);
    void free(void* const alloc);
    void get_offset_and_base(void* alloc, void*& base, size_t& offset);

  private:
    bool allocate_new_page(const RendererMemoryRequirements& reqs);
    void split_bucket(Bucket& bucket, size_t req_size);
    bool merge_buddies(Bucket& bucket);
    void defragment();

    static Bucket get_buddy_bucket(const Bucket& bucket)
    {
        auto buddy = bucket;
        buddy.page_offset ^= bucket.size;
        return buddy;
    }

    static Bucket get_left_split_bucket(const Bucket& bucket)
    {
        auto left = bucket;
        left.page_offset &= ~bucket.size; // remove offset bit that right split bucket has XORed
        return left;
    }

    void* get_bucket_memory(const Bucket& bucket)
    {
        return (void*)((uintptr_t)pages[bucket.page] + bucket.page_offset);
    }

    Callback<void*(const RendererMemoryRequirements&)> allocator;
    std::vector<void*> pages;
    std::set<Bucket> buckets;
    std::unordered_map<void*, Bucket> allocations;
    size_t last_alloc_frame{ 0 };
};

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

template <typename ExecFunc> struct RGUserPass<void, ExecFunc> : public RGPass
{
    RGUserPass(const char* name, Type type, const ExecFunc& exec_func) : RGPass(name, type), exec_func(exec_func) {}
    ~RGUserPass() override = default;
    void execute(RGBuilder& pb) override { exec_func(pb); };
    ExecFunc exec_func{};
};

struct RGClear
{
    struct DepthStencil
    {
        float depth;
        uint32_t stencil;
    };
    struct Color
    {
        glm::vec4 color;
    };
    static RGClear color(std::array<float, 4> color)
    {
        return RGClear{ Color{ glm::vec4{ color[0], color[1], color[2], color[3] } } };
    }
    static RGClear depth_stencil(const DepthStencil& c) { return RGClear{ c }; }
    bool is_color() const { return value.index() == 0; }
    Color get_color() const { return std::get<0>(value); }
    DepthStencil get_ds() const { return std::get<1>(value); }
    std::variant<Color, DepthStencil> value;
};

struct RGResource
{
    using NativeResource = std::variant<Handle<Buffer>, Handle<Image>>;
    bool is_buffer() const { return native.index() == 0; }
    Handle<Buffer> as_buffer() const { return std::get<0>(native); }
    Handle<Image> as_image() const { return std::get<1>(native); }
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
    // note: this might prove to be problematic if some resources will actually need to use none/none (
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

    void init(Renderer* r)
    {
        queue = r->get_queue(QueueType::GRAPHICS);
        sems[0] = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
        sems[1] = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
        cmd_pools[0] = queue->make_command_pool();
        cmd_pools[1] = queue->make_command_pool();
        allocator.init([r](const RendererMemoryRequirements& reqs) {
            return r->backend->allocate_aliasable_memory(reqs);
        });
    }

    template <typename UserType, typename SetupFunc, typename ExecFunc>
    const UserType& add_pass(const char* name, RGPass::PassOrder order, RGPass::Type type, const SetupFunc& setup_func,
                             const ExecFunc& exec_func)
    {
        OrderedPass op{};
        op.pass = std::make_unique<RGUserPass<UserType, ExecFunc>>(name, type, exec_func);
        op.order = order;

        ENG_ASSERT(!namedpasses.contains(op.pass->id));

        auto it = std::upper_bound(passes.begin(), passes.end(), op);
        it = passes.insert(it, std::move(op));

        namedpasses[it->pass->id] = &*it->pass;

        RGBuilder pb{ &*it->pass, this };
        if constexpr(!std::is_same_v<UserType, void>)
        {
            auto* user_data = static_cast<UserType*>(it->pass->get_user_data());
            setup_func(pb, *user_data);
            return (const UserType&)*user_data;
        }
        else { setup_func(pb); }
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

    void compile()
    {
        const auto calc_earliest_group = [this](const std::vector<RGAccessId>& accesses) -> uint32_t {
            return std::accumulate(accesses.begin(), accesses.end(), 0u, [this](auto max, const auto& val) {
                return std::max(max, [this, &val] {
                    const auto& acc = get_acc(val);
                    // access just created the resource, we can start right away.
                    if(acc.is_first_access()) { return 0u; }
                    const auto& res = get_res(val);
                    // if current access is a write, we need to wait for previous reads and writes.
                    if(acc.is_write()) { return std::max(res.last_read_group + 1, res.last_write_group + 1); }
                    if(acc.is_read())
                    {
                        const auto& pacc = get_acc(acc.prev_access);
                        // if reading, we need to at least wait for previous writes, if any
                        auto earliest = res.last_write_group + 1;
                        // if reading, and layouts are not same, change layout and read in the next stage
                        if(pacc.layout != acc.layout) { earliest = std::max(earliest, res.last_read_group + 1); }
                        // if layouts are compatible, we can read at the same time.
                        return earliest;
                    }
                    ENG_ASSERT(false);
                    return ~0u;
                }());
            });
        };
        const auto update_accesses = [this](const std::vector<RGAccessId>& accesses, uint32_t gid) {
            std::for_each(accesses.begin(), accesses.end(), [this, gid](auto a) {
                const auto& acc = get_acc(a);
                auto& res = get_res(a);
                if(acc.is_read()) { res.last_read_group = gid; }
                if(acc.is_write()) { res.last_write_group = gid; }
            });
        };

        groups.clear();
        groups.resize(passes.size());
        uint32_t last_gid = 0;
        for(auto& p : passes)
        {
            const auto gid = calc_earliest_group(p.pass->accesses);
            last_gid = std::max(last_gid, gid);
            groups[gid].passes.push_back(&*p.pass);
            update_accesses(p.pass->accesses, gid);
        }
        groups.resize(last_gid + 1);

        // Allocate memory from transient allocator for resources to be used during execution
        std::set<RGResourceId> alive_res_set;
        std::set<RGResourceId> res_to_remove;
        const auto remove_unused_res = [this, &res_to_remove, &alive_res_set] {
            for(auto e : res_to_remove)
            {
                auto& res = get_res(e);
                alive_res_set.erase(e);
                allocator.free(res.alloc);
                res.alloc = nullptr;
            }
            res_to_remove.clear();
        };
        for(auto& g : groups)
        {
            remove_unused_res();
            for(auto* p : g.passes)
            {
                for(const auto& ra : p->accesses)
                {
                    auto& acc = get_acc(ra);
                    auto& res = get_res(ra);
                    if(res.is_persistent) { continue; }
                    auto insertion = alive_res_set.insert(acc.resource);
                    if(!insertion.second)
                    {
                        if(res.last_access == ra) { res_to_remove.insert(acc.resource); }
                        continue;
                    }
                    // insertion happened, this resource has never had memory bound to it during this frame
                    RendererMemoryRequirements reqs;
                    void* alloc;
                    void* base;
                    size_t offset;
                    if(res.is_buffer())
                    {
                        get_renderer().backend->get_memory_requirements(res.as_buffer().get(), reqs);
                        alloc = allocator.allocate(reqs);
                        allocator.get_offset_and_base(alloc, base, offset);
                        get_renderer().backend->bind_aliasable_memory(res.as_buffer().get(), base, offset);
                    }
                    else
                    {
                        get_renderer().backend->get_memory_requirements(res.as_image().get(), reqs);
                        alloc = allocator.allocate(reqs);
                        allocator.get_offset_and_base(alloc, base, offset);
                        get_renderer().backend->bind_aliasable_memory(res.as_image().get(), base, offset);
                    }
                    res.alloc = alloc;
                }
            }
        }
        remove_unused_res();
    }

    Sync* execute(Sync** wait_syncs = nullptr, uint32_t wait_count = 0)
    {
        std::swap(sems[0], sems[1]);
        std::swap(cmd_pools[0], cmd_pools[1]);
        sems[0]->reset();
        cmd_pools[0]->reset();
        for(auto i = 0u; i < wait_count; ++i)
        {
            queue->wait_sync(wait_syncs[i]);
        }
        for(auto i = 0u; i < groups.size(); ++i)
        {
            const auto& g = groups[i];
            Flags<PipelineStage> gstages;
            ICommandBuffer* layout_cmd = nullptr;

            for(const auto* p : g.passes)
            {
                gstages |= p->stage_mask;
                for(const auto pa : p->accesses)
                {
                    auto& acc = get_acc(pa);
                    const auto& res = get_res(pa);
                    if(res.is_buffer()) { continue; }
                    if(acc.is_first_access())
                    {
                        if(res.clear)
                        {
                            if(!layout_cmd) { layout_cmd = cmd_pools[0]->begin(); }
                            if(res.clear->is_color())
                            {
                                acc.stage = PipelineStage::TRANSFER_BIT;
                                acc.access = PipelineAccess::TRANSFER_WRITE_BIT;
                                acc.layout = ImageLayout::TRANSFER_DST;
                                const auto clear_color = res.clear->get_color().color;
                                layout_cmd->barrier(res.as_image().get(), PipelineStage::NONE, PipelineAccess::NONE,
                                                    PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                                                    ImageLayout::UNDEFINED, ImageLayout::TRANSFER_DST);
                                layout_cmd->clear_color(res.as_image().get(), Color4f{ clear_color.x, clear_color.y,
                                                                                       clear_color.z, clear_color.w });
                            }
                            else { ENG_ERROR(); }
                        }
                        continue;
                    }
                    const auto& pacc = get_acc(acc.prev_access);
                    if(pacc.layout == acc.layout) { continue; }
                    if(!layout_cmd) { layout_cmd = cmd_pools[0]->begin(); }
                    Handle<Image> img = acc.image_view.image;
                    layout_cmd->barrier(img.get(), pacc.stage, pacc.access, acc.stage, acc.access, pacc.layout, acc.layout);
                }
            }

            if(layout_cmd)
            {
                cmd_pools[0]->end(layout_cmd);
                queue->with_cmd_buf(layout_cmd);
                queue->submit();
            }

            for(auto* p : g.passes)
            {
                RGBuilder pb{ p, this };
                p->execute(pb);
                if(!p->cmd) { continue; }
                p->cmd->end_label();
                cmd_pools[0]->end(p->cmd);
                queue->with_cmd_buf(p->cmd);

                for(const auto& ra : p->accesses)
                {
                    auto& res = get_res(ra);
                    // todo: maybe pool handles here? renderer already does that, soo...
                    // also, maybe add resource set in pass, because iterating over accesses is going over same resource multiple times (potentially)
                    if(!res.is_persistent && res.last_access == ra) { destroy_resource(res); }
                }
            }
            queue->wait_sync(sems[0], gstages);
            queue->signal_sync(sems[0], gstages);
            queue->submit();
        }

        resources.clear();
        accesses.clear();
        passes.clear();
        namedpasses.clear();

        return sems[0];
    }

    void destroy_resource(RGResource& res)
    {
        ENG_ASSERT(!res.is_persistent);
        auto& r = get_renderer();
        if(res.is_buffer())
        {
            auto h = res.as_buffer();
            r.destroy_buffer(h);
        }
        else
        {
            auto h = res.as_image();
            r.destroy_image(h);
        }
    }

    // template <typename UserData> UserData* get_user_data() const { return *RGUserDataStoringPass<UserData>::ptr; }

    SubmitQueue* queue{};
    ICommandPool* cmd_pools[2]{};
    Sync* sems[2]{};
    GPUTransientAllocator allocator;

    std::unordered_map<std::pair<RGPass::PassId, uint64_t>, PersistentStorage, hash::PairHash> persistent_resources;
    std::vector<RGResource> resources;
    std::vector<RGAccess> accesses;
    std::vector<OrderedPass> passes;
    std::vector<ExecutionGroup> groups;
    std::unordered_map<RGPass::PassId, RGPass*> namedpasses;
};

inline bool GPUTransientAllocator::Bucket::is_free() const
{
    // buckets are reused across frames. to make sure the previous frame is not using this bucket, when the next frame tries to allocate it,
    // we check if it either has never been touched, or it was freed in the current frame, or if enough frames have passed since.
    const auto frame = get_renderer().current_frame;
    const auto wait = get_renderer().frame_delay;
    return freed_at == ~0ull || freed_at == frame || (frame - freed_at) >= wait;
}

inline bool GPUTransientAllocator::Bucket::can_be_merged_with_buddy(const Bucket& buddy) const
{
    ENG_ASSERT(page == buddy.page && size == buddy.size);
    // can only merge up if enough frames have passed or buckets were freed during the same frame. otherwise it will consolidate back to one page-sized
    // bucket with is_free() returning false for the next frame, even though the previous frame only used 64kbs.
    const auto frame = get_renderer().current_frame;
    const auto wait = get_renderer().frame_delay;
    return freed_at == buddy.freed_at || (freed_at == ~0ull && (frame - buddy.freed_at) >= wait) ||
           (buddy.freed_at == ~0ull && (frame - freed_at) >= wait) ||
           (frame - freed_at >= wait && frame - buddy.freed_at >= wait);
}

inline void* GPUTransientAllocator::allocate(const RendererMemoryRequirements& reqs)
{
    // todo: this should also somehow check memory types whether the resource will be compatible
    //       with the memory the allocator is managing.
    ENG_ASSERT(reqs.size > 0 && reqs.alignment > 0 && is_pow2(reqs.alignment));
    // 65kbs rounds up to 128kbs :(
    const auto aligned = next_power_of_2(std::max(align_up2(reqs.size, reqs.alignment), MIN_ALLOC));
    if(PAGE_SIZE < aligned)
    {
        ENG_ASSERT(false, "Allocation too big for page");
        return nullptr;
    }
    const auto frame = get_renderer().current_frame;
    if(last_alloc_frame != frame)
    {
        // first alloc of render frame, try defrag.
        last_alloc_frame = frame;
        defragment();
    }
    while(true)
    {
        // find smallest possible
        auto it = buckets.lower_bound(Bucket{ .page = 0, .page_offset = 0, .size = aligned });
        // go up if not enough render frames have passed and the resource may still be in use, unless
        // it's the same frame, then access is guarded by barriers/semaphores in the rendergraph.
        for(; it != buckets.end() && !it->is_free(); ++it) {}
        if(it == buckets.end())
        {
            // no free bucket, allocate new page
            if(!allocate_new_page(reqs)) { return nullptr; }
            continue;
        }
        auto bucket = *it;
        buckets.erase(it);
        split_bucket(bucket, aligned); // keep cutting off right halfs until just right
        auto* const alloc = get_bucket_memory(bucket);
        ENG_ASSERT(!allocations.contains(alloc));
        allocations[alloc] = bucket;
        return alloc;
    }
}

inline void GPUTransientAllocator::free(void* const alloc)
{
    if(!alloc)
    {
        ENG_ASSERT(false, "Alloc is null");
        return;
    }

    auto allocit = allocations.find(alloc);
    if(allocit == allocations.end())
    {
        ENG_ASSERT(false, "Invalid alloc");
        return;
    }

    auto bucket = allocit->second;
    allocations.erase(allocit);
    bucket.freed_at = get_renderer().current_frame;
    merge_buddies(bucket); // merge freed this frame for better reuse within one frame
    buckets.insert(bucket);
}

inline void GPUTransientAllocator::get_offset_and_base(void* alloc, void*& base, size_t& offset)
{
    ENG_ASSERT(alloc);
    const auto& allocation = allocations.at(alloc);
    base = pages[allocation.page];
    offset = allocation.page_offset;
}

inline bool gfx::GPUTransientAllocator::allocate_new_page(const RendererMemoryRequirements& reqs)
{
    if(PAGE_SIZE < reqs.size && PAGE_ALIGNMENT < reqs.alignment)
    {
        ENG_ERROR("Bad alignment or size");
        return false;
    }
    auto page_reqs = reqs;
    page_reqs.size = PAGE_SIZE;
    page_reqs.alignment = PAGE_ALIGNMENT;
    void* page = allocator(page_reqs);
    if(!page)
    {
        ENG_ERROR("Could not allocate new page");
        return false;
    }
    pages.push_back(page);
    buckets.insert(Bucket{ .page = (uint32_t)pages.size() - 1, .page_offset = 0, .size = PAGE_SIZE });
    return true;
}

inline void gfx::GPUTransientAllocator::split_bucket(Bucket& bucket, size_t req_size)
{
    ENG_ASSERT(!buckets.contains(bucket));
    auto split_size = bucket.size >> 1;
    while(req_size <= split_size)
    {
        // if the half of the bucket is enough, split and try again with the left half again.
        bucket.size = split_size;
        bucket = get_left_split_bucket(bucket);
        buckets.insert(get_buddy_bucket(bucket));
        split_size >>= 1;
    }
}

inline bool gfx::GPUTransientAllocator::merge_buddies(Bucket& bucket)
{
    bool merged = false;
    while(true)
    {
        auto buddyit = buckets.find(get_buddy_bucket(bucket));
        if(buddyit == buckets.end() || !buddyit->can_be_merged_with_buddy(bucket)) { return merged; }
        merged = true;
        buckets.erase(buddyit);
        bucket = get_left_split_bucket(bucket);
        bucket.size <<= 1;
    }
}

inline void GPUTransientAllocator::defragment()
{
    std::vector<Bucket> candidates;
    candidates.reserve(buckets.size());
    for(const auto& b : buckets)
    {
        if(b.is_free()) { candidates.push_back(b); }
    }
    for(auto& b : candidates)
    {
        if(!buckets.contains(b)) { continue; }
        buckets.erase(b);
        merge_buddies(b);
        buckets.insert(b);
    }
}

inline RGAccessId RGBuilder::add_resource(const RGResource& resource, const std::optional<RGClear>& clear)
{
    ENG_ASSERT(!clear);
    const auto layout = resource.is_buffer() ? ImageLayout::UNDEFINED : resource.as_image()->layout;
    graph->resources.push_back(resource);
    const auto ret = add_access(RGAccess{
        .resource = RGResourceId{ (uint32_t)graph->resources.size() - 1 },
        .layout = layout,
        .stage = {},
        .access = {},
    });
    // if(clear) { p->clears.emplace_back(ret, *clear); }
    return ret;
}

inline RGAccessId RGBuilder::import_resource(const RGResource::NativeResource& resource, const std::optional<RGClear>& clear)
{
    const auto it = std::find_if(graph->resources.begin(), graph->resources.end(),
                                 [&resource](const auto& e) { return e.native == resource; });
    if(it != graph->resources.end()) { return it->last_access; }
    return add_resource(RGResource{ .native = resource, .is_persistent = true, .is_imported = true });
}

inline PersistentStorage* RGBuilder::find_persistent(uint64_t namehash)
{
    auto it = graph->persistent_resources.find(std::make_pair(pass->id, namehash));
    if(it == graph->persistent_resources.end()) { return nullptr; }
    return &it->second;
}

inline RGAccessId RGBuilder::create_resource(const char* name, Buffer&& a, bool persistent)
{
    ENG_ASSERT(name);
    RGResource::NativeResource native = [this, &a, &name, persistent] -> RGResource::NativeResource {
        if(persistent)
        {
            const auto hash = ENG_HASH_STR(name);
            if(auto* p = find_persistent(hash)) { return p->native; }
            auto native_handle = get_engine().renderer->make_buffer(name, std::move(a));
            auto persistent_it = graph->persistent_resources.emplace(std::make_pair(pass->id, hash),
                                                                     PersistentStorage{ .native = native_handle });
            return persistent_it.first->second.native;
        }
        return get_engine().renderer->make_buffer(name, std::move(a), AllocateMemory::ALIASED);
    }();
    return add_resource(RGResource{ .native = native, .is_persistent = persistent });
}

inline RGAccessId RGBuilder::create_resource(const char* name, Image&& a, bool persistent, const std::optional<RGClear>& clear)
{
    ENG_ASSERT(name);
    RGResource::NativeResource native = [this, &a, &name, persistent] -> RGResource::NativeResource {
        if(persistent)
        {
            const auto hash = ENG_HASH_STR(name);
            if(auto* p = find_persistent(hash)) { return p->native; }
            auto native_handle = get_engine().renderer->make_image(name, std::move(a));
            auto persistent_it = graph->persistent_resources.emplace(std::make_pair(pass->id, hash),
                                                                     PersistentStorage{ .native = native_handle });
            return persistent_it.first->second.native;
        }
        return get_engine().renderer->make_image(name, std::move(a), AllocateMemory::ALIASED);
    }();
    return add_resource(RGResource{ .native = native, .is_persistent = persistent, .clear = clear });
}

inline RGAccessId RGBuilder::add_access(const RGAccess& a)
{
    graph->accesses.push_back(a);
    const auto ret = RGAccessId{ (uint32_t)graph->accesses.size() - 1 };
    graph->get_res(ret).last_access = ret;
    pass->accesses.push_back(ret);
    pass->stage_mask |= a.stage;
    return ret;
}

inline RGAccessId RGBuilder::access_resource(RGAccessId acc, ImageLayout layout, Flags<PipelineStage> stage,
                                             Flags<PipelineAccess> access, std::optional<ImageFormat> format,
                                             std::optional<ImageViewType> type, Range32u mips, Range32u layers)
{
    return add_access(RGAccess{
        .resource = graph->get_acc(acc).resource,
        .prev_access = acc,
        .image_view = ImageView::init(graph->get_img(acc), format, type, mips.offset, mips.size, layers.offset, layers.size),
        .layout = layout,
        .stage = stage,
        .access = access,
    });
}

inline RGAccessId RGBuilder::access_resource(RGAccessId acc, Flags<PipelineStage> stage, Flags<PipelineAccess> access, Range64u range)
{
    return add_access(RGAccess{
        .resource = graph->get_acc(acc).resource,
        .prev_access = acc,
        .buffer_view = BufferView{ graph->get_buf(acc), range },
        .stage = stage,
        .access = access,
    });
}

inline ICommandBuffer* RGBuilder::open_cmd_buf()
{
    ENG_ASSERT(pass->cmd == nullptr);
    pass->cmd = graph->cmd_pools[0]->begin();
    pass->cmd->begin_label(pass->name.c_str());
    return pass->cmd;
}

} // namespace gfx
} // namespace eng