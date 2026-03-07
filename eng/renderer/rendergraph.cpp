#include "rendergraph.hpp"

#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <eng/common/handle.hpp>
#include <eng/engine.hpp>

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
    GPUTransientAllocator(const auto& alias_allocator) { allocator = alias_allocator; }

    void* allocate(const RendererMemoryRequirements& reqs)
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

RGAccessId RGBuilder::add_resource(const RGResource& resource, const std::optional<RGClear>& clear)
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

RGAccessId RGBuilder::import_resource(const RGResource::NativeResource& resource, const std::optional<RGClear>& clear)
{
    const auto it = std::find_if(graph->resources.begin(), graph->resources.end(),
                                 [&resource](const auto& e) { return e.native == resource; });
    if(it != graph->resources.end()) { return it->last_access; }
    return add_resource(RGResource("", resource, true, true, clear));
}

PersistentStorage* RGBuilder::find_persistent(uint64_t namehash)
{
    auto it = graph->persistent_resources.find(std::make_pair(pass->id, namehash));
    if(it == graph->persistent_resources.end()) { return nullptr; }
    return &it->second;
}

RGAccessId RGBuilder::create_resource(const char* name, Buffer&& a, bool persistent)
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
    return add_resource(RGResource(name, native, persistent, false));
}

RGAccessId RGBuilder::create_resource(const char* name, Image&& a, bool persistent, const std::optional<RGClear>& clear)
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
    return add_resource(RGResource(name, native, persistent, false, clear));
}

RGAccessId RGBuilder::add_access(const RGAccess& a)
{
    const bool already_contains_resource = std::any_of(pass->accesses.begin(), pass->accesses.end(), [this, &a](RGAccessId id) {
        auto& acc = graph->get_acc(id);
        return !acc.is_first_access() && acc.resource == a.resource;
    });
    if(already_contains_resource)
    {
        ENG_ERROR("Pass \"{}\" already references this resource (\"{}\")", pass->name.as_view(),
                  graph->get_res(a.resource).name.as_view());
        return {};
    }

    graph->accesses.push_back(a);
    const auto ret = RGAccessId{ (uint32_t)graph->accesses.size() - 1 };
    graph->get_res(ret).last_access = ret;
    pass->accesses.push_back(ret);
    pass->stage_mask |= a.stage;
    return ret;
}

RGAccessId RGBuilder::access_resource(RGAccessId acc, ImageLayout layout, Flags<PipelineStage> stage,
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

RGAccessId RGBuilder::access_resource(RGAccessId acc, Flags<PipelineStage> stage, Flags<PipelineAccess> access, Range64u range)
{
    return add_access(RGAccess{
        .resource = graph->get_acc(acc).resource,
        .prev_access = acc,
        .buffer_view = BufferView{ graph->get_buf(acc), range },
        .stage = stage,
        .access = access,
    });
}

ICommandBuffer* RGBuilder::open_cmd_buf()
{
    ENG_ASSERT(pass->cmd == nullptr);
    pass->cmd = graph->cmd_pools[0]->begin();
    pass->cmd->begin_label(pass->name.c_str());
    return pass->cmd;
}

void RGRenderGraph::init(Renderer* r)
{
    queue = r->get_queue(QueueType::GRAPHICS);
    sems[0] = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
    sems[1] = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
    cmd_pools[0] = queue->make_command_pool();
    cmd_pools[1] = queue->make_command_pool();
    allocator = new GPUTransientAllocator{ [r](const RendererMemoryRequirements& reqs) {
        return r->backend->allocate_aliasable_memory(reqs);
    } };
}

void RGRenderGraph::compile()
{
    const auto group_passes = [this] {
        const auto get_earliest_group_for_pass = [this](const std::vector<RGAccessId>& accesses) -> uint32_t {
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
        const auto update_resource_accesses = [this](const std::vector<RGAccessId>& accesses, uint32_t last_group) {
            std::for_each(accesses.begin(), accesses.end(), [this, last_group](RGAccessId a) {
                const auto& acc = get_acc(a);
                auto& res = get_res(a);
                if(acc.is_read()) { res.last_read_group = last_group; }
                if(acc.is_write()) { res.last_write_group = last_group; }
            });
        };
        groups.clear();
        groups.resize(passes.size());
        uint32_t last_gid = 0;
        for(auto& p : passes)
        {
            const auto gid = get_earliest_group_for_pass(p.pass->accesses);
            last_gid = std::max(last_gid, gid);
            groups[gid].passes.push_back(&*p.pass);
            update_resource_accesses(p.pass->accesses, gid);
        }
        groups.resize(last_gid + 1);
    };
    const auto bind_aliased_memory_to_resources = [this] {
        // Allocate memory from transient allocator for resources to be used during execution
        std::set<RGResourceId> alive_res_set;
        std::set<RGResourceId> res_to_remove;
        const auto remove_unused_res = [this, &res_to_remove, &alive_res_set] {
            for(auto e : res_to_remove)
            {
                auto& res = get_res(e);
                alive_res_set.erase(e);
                allocator->free(res.alloc);
                res.alloc = nullptr;
            }
            res_to_remove.clear();
        };
        for(auto& g : groups)
        {
            remove_unused_res(); // remove from previous group
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
                        alloc = allocator->allocate(reqs);
                        allocator->get_offset_and_base(alloc, base, offset);
                        get_renderer().backend->bind_aliasable_memory(res.as_buffer().get(), base, offset);
                    }
                    else
                    {
                        get_renderer().backend->get_memory_requirements(res.as_image().get(), reqs);
                        alloc = allocator->allocate(reqs);
                        allocator->get_offset_and_base(alloc, base, offset);
                        get_renderer().backend->bind_aliasable_memory(res.as_image().get(), base, offset);
                    }
                    res.alloc = alloc;
                }
            }
        }
        remove_unused_res();
    };

    group_passes();
    bind_aliased_memory_to_resources();
}

Sync* RGRenderGraph::execute(Sync** wait_syncs, uint32_t wait_count)
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
                            layout_cmd->clear_color(res.as_image().get(),
                                                    Color4f{ clear_color.x, clear_color.y, clear_color.z, clear_color.w });
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

void RGRenderGraph::destroy_resource(RGResource& res)
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

} // namespace gfx
} // namespace eng