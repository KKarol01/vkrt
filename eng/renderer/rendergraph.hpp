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
#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/renderer_vulkan.hpp>

namespace eng
{
namespace gfx
{

class RenderGraph
{
  public:
    struct Resource;
    struct ResourceAccess;
    struct PassBuilder;

    struct Clear
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
        static Clear color(const Color& c) { return Clear{ c }; }
        static Clear depth_stencil(const DepthStencil& c) { return Clear{ c }; }
        std::variant<Color, DepthStencil> value;
    };

    using SetupFunc = Callback<void(PassBuilder&)>;
    using ExecFunc = Callback<void(RenderGraph&, PassBuilder&)>;
    using PassDataDeleter = Callback<void(void*)>;

    struct Pass
    {
        enum class Type
        {
            NONE,
            GRAPHICS,
            COMPUTE,
        };
        bool is_graphics() const { return type == Type::GRAPHICS; }
        bool is_compute() const { return type == Type::COMPUTE; }
        std::string name;
        Type type{ Type::NONE };
        ExecFunc exec_cb{};
        std::vector<Handle<ResourceAccess>> accesses;
        std::vector<std::pair<Handle<ResourceAccess>, Clear>> clears;
        Flags<PipelineStage> stage_mask{};
        ICommandBuffer* cmd{};
        std::unique_ptr<void, PassDataDeleter> pass_data;
    };

    struct Resource
    {
        using NativeResource = std::variant<Handle<Buffer>, Handle<Image>>;
        bool is_buffer() const { return native.index() == 0; }
        Handle<Buffer> as_buffer() const { return std::get<0>(native); }
        Handle<Image> as_image() const { return std::get<1>(native); }
        NativeResource native;
        Handle<ResourceAccess> last_access;
        uint32_t last_read_group{ ~0u };
        uint32_t last_write_group{ ~0u };
        bool is_persistent{};
        bool is_imported{};
        void* alloc{}; // from transient allocator if not persistent
    };

    struct ResourceAccess
    {
        bool is_read() const { return (access & PipelineAccess::READS) > 0; }
        bool is_write() const { return (access & PipelineAccess::WRITES) > 0; }
        // note: this might prove to be problematic if some resources will actually need to use none/none (
        bool is_import_only() const { return !prev_access; }
        Handle<Resource> resource;
        Handle<ResourceAccess> prev_access;
        union {
            BufferView buffer_view; // if resource->is_buffer() == true or layout == undefined
            ImageView image_view;
        };
        ImageLayout layout{ ImageLayout::UNDEFINED };
        Flags<PipelineStage> stage;
        Flags<PipelineAccess> access;
    };

    struct ExecutionGroup
    {
        // Sync* sync{};
        std::vector<Pass*> passes;
    };

    struct PersistentStorage
    {
        std::string pass_name;
        Resource::NativeResource native;
    };

    struct PassBuilder
    {

        Handle<ResourceAccess> add_resource(const Resource& resource, const std::optional<Clear>& clear = {})
        {
            graph->resources.push_back(resource);
            const auto ret = add_access(ResourceAccess{
                .resource = Handle<Resource>{ (uint32_t)graph->resources.size() - 1 },
                .layout = ImageLayout::UNDEFINED,
                .stage = {},
                .access = {},
            });
            ENG_ASSERT(!clear);
            ENG_LOG("Making resource {}", (resource.is_buffer() ? resource.as_buffer()->name : resource.as_image()->name));
            // if(clear) { p->clears.emplace_back(ret, *clear); }
            return ret;
        }

        Handle<ResourceAccess> import_resource(const Resource::NativeResource& resource, const std::optional<Clear>& clear = {})
        {
            const auto it = std::find_if(graph->resources.begin(), graph->resources.end(),
                                         [&resource](const auto& e) { return e.native == resource; });
            if(it != graph->resources.end()) { return it->last_access; }
            return add_resource(Resource{ .native = resource, .is_persistent = true, .is_imported = true });
        }

        PersistentStorage* find_persistent(size_t hash, const std::string& pass_name)
        {
            auto it = graph->persistent_resources.find(hash);
            if(it == graph->persistent_resources.end()) { return nullptr; }
            if(it->second.pass_name != pass_name)
            {
                ENG_ERROR("Hash collision");
                return nullptr;
            }
            return &it->second;
        }

        Handle<ResourceAccess> create_resource(Buffer&& a, bool persistent = false)
        {
            ENG_ASSERT(a.name.size() > 0);
            Resource::NativeResource native = [this, &a, persistent] -> Resource::NativeResource {
                if(persistent)
                {
                    const auto hash = hash::combine_fnv1a(pass->name, a.name);
                    if(auto* p = find_persistent(hash, pass->name)) { return p->native; }
                    auto native_handle = Engine::get().renderer->make_buffer(std::move(a));
                    auto persistent_it =
                        graph->persistent_resources.emplace(hash, PersistentStorage{ .pass_name = pass->name, .native = native_handle });
                    return persistent_it.first->second.native;
                }
                return Engine::get().renderer->make_buffer(std::move(a), AllocateMemory::NO);
            }();
            return add_resource(Resource{ .native = native, .is_persistent = persistent });
        }

        Handle<ResourceAccess> create_resource(Image&& a, bool persistent = false, const std::optional<Clear>& clear = {})
        {
            ENG_ASSERT(a.name.size() > 0);
            Resource::NativeResource native = [this, &a, persistent] -> Resource::NativeResource {
                if(persistent)
                {
                    const auto hash = hash::combine_fnv1a(pass->name, a.name);
                    if(auto* p = find_persistent(hash, pass->name)) { return p->native; }
                    auto native_handle = Engine::get().renderer->make_image(std::move(a));
                    auto persistent_it =
                        graph->persistent_resources.emplace(hash, PersistentStorage{ .pass_name = pass->name, .native = native_handle });
                    return persistent_it.first->second.native;
                }
                return Engine::get().renderer->make_image(std::move(a), AllocateMemory::NO);
            }();
            return add_resource(Resource{ .native = native, .is_persistent = persistent });
        }

        Handle<ResourceAccess> add_access(const ResourceAccess& a)
        {
            graph->accesses.push_back(a);
            const auto ret = Handle<ResourceAccess>{ (uint32_t)graph->accesses.size() - 1 };
            graph->get_res(ret).last_access = ret;
            pass->accesses.push_back(ret);
            pass->stage_mask |= a.stage;
            return ret;
        }

        Handle<ResourceAccess> sample_texture(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                              std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                              Range32u layers = { 0u, ~0u })
        {
            const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                               : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                    : PipelineStage::NONE;
            const auto access = PipelineAccess::SHADER_READ_BIT;
            const auto layout = ImageLayout::READ_ONLY;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> access_depth(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {})
        {
            const auto stage = PipelineStage::EARLY_Z_BIT | PipelineStage::LATE_Z_BIT;
            const auto access = PipelineAccess::DS_RW;
            const auto layout = ImageLayout::ATTACHMENT;
            return access_resource(acc, layout, stage, access, format, ImageViewType::TYPE_2D);
        }

        Handle<ResourceAccess> access_color(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                            std::optional<ImageViewType> type = {})
        {
            const auto stage = PipelineStage::COLOR_OUT_BIT;
            const auto access = PipelineAccess::COLOR_RW_BIT;
            const auto layout = ImageLayout::ATTACHMENT;
            return access_resource(acc, layout, stage, access, format, ImageViewType::TYPE_2D);
        }

        Handle<ResourceAccess> read_image(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                          std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                          Range32u layers = { 0u, ~0u })
        {
            const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                               : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                    : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_READ_BIT;
            const auto layout = ImageLayout::GENERAL;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> write_image(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                           std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                           Range32u layers = { 0u, ~0u })
        {
            const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                               : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                    : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_WRITE_BIT;
            const auto layout = ImageLayout::GENERAL;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> read_write_image(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                                std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                                Range32u layers = { 0u, ~0u })
        {
            const auto stage = pass->is_graphics()  ? PipelineStage::FRAGMENT
                               : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                    : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_RW;
            const auto layout = ImageLayout::GENERAL;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> read_buffer(Handle<ResourceAccess> acc, Range64u range = { 0ull, ~0ull })
        {
            const auto stage = pass->is_graphics()  ? PipelineStage::VERTEX_BIT | PipelineStage::FRAGMENT
                               : pass->is_compute() ? PipelineStage::COMPUTE_BIT
                                                    : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_READ_BIT;
            return access_resource(acc, stage, access, range);
        }

        Handle<ResourceAccess> write_buffer(Handle<ResourceAccess> acc, Range64u range = { 0ull, ~0ull })
        {
            const auto stage = pass->is_compute() ? PipelineStage::COMPUTE_BIT : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_RW;
            return access_resource(acc, stage, access, range);
        }

        Handle<ResourceAccess> read_write_buffer(Handle<ResourceAccess> acc, Range64u range = { 0ull, ~0ull })
        {
            const auto stage = pass->is_compute() ? PipelineStage::COMPUTE_BIT : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_RW;
            return access_resource(acc, stage, access, range);
        }

        Handle<ResourceAccess> access_resource(Handle<ResourceAccess> acc, ImageLayout layout, Flags<PipelineStage> stage,
                                               Flags<PipelineAccess> access, std::optional<ImageFormat> format = {},
                                               std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                               Range32u layers = { 0u, ~0u })
        {
            return add_access(ResourceAccess{
                .resource = graph->get_acc(acc).resource,
                .prev_access = acc,
                .image_view =
                    ImageView::init(graph->get_img(acc), format, type, mips.offset, mips.size, layers.offset, layers.size),
                .layout = layout,
                .stage = stage,
                .access = access,
            });
        }

        Handle<ResourceAccess> access_resource(Handle<ResourceAccess> acc, Flags<PipelineStage> stage,
                                               Flags<PipelineAccess> access, Range64u range = { 0ull, ~0ull })
        {
            return add_access(ResourceAccess{
                .resource = graph->get_acc(acc).resource,
                .prev_access = acc,
                .buffer_view = BufferView{ graph->get_buf(acc), range },
                .stage = stage,
                .access = access,
            });
        }

        ICommandBuffer* open_cmd_buf()
        {
            ENG_ASSERT(pass->cmd == nullptr);
            pass->cmd = graph->cmd_pools[0]->begin();
            pass->cmd->begin_label(pass->name);
            return pass->cmd;
        }

        Pass* pass{};
        RenderGraph* graph{};
    };

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
    class TransientAllocator
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
            bool is_free() const
            {
                // buckets are reused across frames. to make sure the previous frame is not using this bucket, when the next frame tries to allocate it,
                // we check if it either has never been touched, or it was freed in the current frame, or if enough frames have passed since.
                const auto frame = get_renderer().current_frame;
                const auto wait = get_renderer().frame_delay;
                return freed_at == ~0ull || freed_at == frame || (frame - freed_at) >= wait;
            }
            bool can_be_merged_with_buddy(const Bucket& buddy) const
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

        void* allocate(const RendererMemoryRequirements& reqs)
        {
            // todo: this should also somehow check memory types whether the resource will be compatible
            //       with the memory the allocator is managing.
            ENG_ASSERT(reqs.size > 0 && reqs.alignment > 0 && is_pow2(reqs.alignment));
            const auto aligned =
                next_power_of_2(std::max(align_up2(reqs.size, reqs.alignment), MIN_ALLOC)); // 65kbs rounds up to 128kbs :(
            ENG_ASSERT(is_pow2(aligned));
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
                auto* const alloc = get_bucket_address(bucket);
                ENG_ASSERT(!allocations.contains(alloc));
                allocations[alloc] = bucket;
                return alloc;
            }
        }

        void free(void* const alloc)
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

        void get_offset_and_base(void* alloc, void*& base, size_t& offset)
        {
            ENG_ASSERT(alloc);
            const auto& allocation = allocations.at(alloc);
            base = pages[allocation.page];
            offset = allocation.page_offset;
        }

      private:
        bool allocate_new_page(const RendererMemoryRequirements& reqs)
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

        void split_bucket(Bucket& bucket, size_t req_size)
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

        bool merge_buddies(Bucket& bucket)
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

        void defragment()
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

        void* get_bucket_address(const Bucket& bucket)
        {
            return (void*)((uintptr_t)pages[bucket.page] + bucket.page_offset);
        }

        Callback<void*(const RendererMemoryRequirements&)> allocator;
        std::vector<void*> pages;
        std::set<Bucket> buckets;
        std::unordered_map<void*, Bucket> allocations;
        size_t last_alloc_frame{ 0 };
    };

    // utility funcs for easy access to resources
    ResourceAccess& get_acc(Handle<ResourceAccess> a) { return accesses[*a]; }
    Resource& get_res(Handle<Resource> a) { return resources[*a]; }
    Resource& get_res(Handle<ResourceAccess> a) { return resources[*get_acc(a).resource]; }
    Handle<Buffer> get_buf(Handle<ResourceAccess> a) { return get_res(a).as_buffer(); }
    Handle<Image> get_img(Handle<ResourceAccess> a) { return get_res(a).as_image(); }

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

    void add_graphics_pass(std::string_view name, const SetupFunc& setup_cb, const ExecFunc& exec_cb)
    {
        passes.push_back(Pass{ .name = { name.data(), name.size() }, .type = Pass::Type::GRAPHICS, .exec_cb = exec_cb });
        PassBuilder pb{ &passes.back(), this };
        return setup_cb(pb);
    }

    void add_compute_pass(std::string_view name, const SetupFunc& setup_cb, const ExecFunc& exec_cb)
    {
        passes.push_back(Pass{ .name = { name.data(), name.size() }, .type = Pass::Type::COMPUTE, .exec_cb = exec_cb });
        PassBuilder pb{ &passes.back(), this };
        return setup_cb(pb);
    }

    void compile()
    {
        const auto calc_earliest_group = [this](const std::vector<Handle<ResourceAccess>>& accesses) {
            return std::accumulate(accesses.begin(), accesses.end(), 0u, [this](auto max, const auto& val) {
                return std::max(max, [this, &val] {
                    const auto& acc = get_acc(val);
                    // access just created the resource, we can start right away.
                    if(acc.is_import_only()) { return 0u; }
                    const auto& res = get_res(val);
                    // if current access is a write, we need to wait for previous reads and writes.
                    if(acc.is_write()) { return std::max(res.last_read_group + 1, res.last_write_group + 1); }
                    if(acc.is_read())
                    {
                        const auto& pacc = get_acc(acc.prev_access);
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
        const auto update_access_groups = [this](const std::vector<Handle<ResourceAccess>>& accesses, uint32_t gid) {
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
            const auto gid = calc_earliest_group(p.accesses);
            last_gid = std::max(last_gid, gid);
            groups[gid].passes.push_back(&p);
            update_access_groups(p.accesses, gid);
        }
        groups.resize(last_gid + 1);

        std::set<Handle<Resource>> alive_res_set;
        for(auto gi = 0ull; gi < groups.size(); ++gi)
        {
            auto& g = groups[gi];
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
                        if(res.last_access == ra)
                        {
                            alive_res_set.erase(acc.resource);
                            allocator.free(res.alloc);
                            res.alloc = nullptr;
                        }
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
        for(const auto& rh : alive_res_set)
        {
            auto& res = get_res(rh);
            allocator.free(res.alloc);
            res.alloc = nullptr;
        }
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
                    const auto& acc = get_acc(pa);
                    const auto& res = get_res(pa);
                    if(res.is_buffer()) { continue; }
                    if(acc.is_import_only()) { continue; }
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
                PassBuilder pb{ p, this };
                p->exec_cb(*this, pb);
                if(!p->cmd) { continue; }
                p->cmd->end_label();
                cmd_pools[0]->end(p->cmd);
                queue->with_cmd_buf(p->cmd);

                for(const auto& ra : p->accesses)
                {
                    auto& res = get_res(ra);
                    // todo: maybe pool handles here? renderer already does that, soo...
                    // also, maybe add resource set in pass, because iterating over accesses is going over same resource multiple times (potentially)
                    if(!res.is_persistent && !res.is_imported && res.last_access == ra) { destroy_resource(res); }
                }
            }
            queue->wait_sync(sems[0], gstages);
            queue->signal_sync(sems[0], gstages);
            queue->submit();
        }

        resources.clear();
        accesses.clear();
        passes.clear();

        return sems[0];
    }

    void destroy_resource(Resource& res)
    {
        ENG_ASSERT(!res.is_persistent);
        auto& r = get_renderer();
        if(res.is_buffer())
        {
            auto h = res.as_buffer();
            ENG_LOG("Destroying resource {}", h->name);
            r.destroy_buffer(h);
        }
        else
        {
            auto h = res.as_image();
            ENG_LOG("Destroying resource {}", h->name);
            r.destroy_image(h);
        }
    }

    SubmitQueue* queue{};
    ICommandPool* cmd_pools[2]{};
    Sync* sems[2]{};
    TransientAllocator allocator;

    std::unordered_map<uint64_t, PersistentStorage> persistent_resources;
    std::vector<Resource> resources;
    std::vector<ResourceAccess> accesses;
    std::vector<Pass> passes;
    std::unordered_map<std::string, Handle<Pass>> namedpasses;
    std::vector<ExecutionGroup> groups;
};

} // namespace gfx
} // namespace eng