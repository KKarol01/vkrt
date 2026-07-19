#include "rendergraph.hpp"

#include <unordered_set>
#include <unordered_map>
#include <set>
#include <eng/common/handle.hpp>
#include <eng/engine.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan/vulkan_backend.hpp>

#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

namespace eng
{
namespace gfx
{

/**
Paged allocator for aliased allocation and deallocation
when compiling rendergraph.
 */
class GPUTransientAllocator
{
    inline static constexpr size_t PAGE_SIZE = 64 * 1024 * 1024;
    inline static constexpr size_t PAGE_ALIGNMENT = 1024 * 1024;

    struct Page
    {
        void* memory{};
        VmaVirtualBlock block{};
    };

    struct Allocation
    {
        Page* page{};
        VmaVirtualAllocation alloc{};
        size_t offset{};
        size_t size{};
    };

  public:
    GPUTransientAllocator(const auto& alias_allocator) { allocator = alias_allocator; }

    void* allocate(RendererMemoryRequirements& reqs)
    {
        // todo: this should also somehow check memory types whether the resource will be compatible
        //       with the memory the allocator is managing.
        ENG_ASSERT(reqs.size > 0 && reqs.alignment > 0 && is_pow2(reqs.alignment));
        while(true)
        {
            for(auto& p : pages)
            {
                VmaVirtualAllocationCreateInfo info{};
                reqs.size = align_up2(reqs.size, PAGE_ALIGNMENT);
                info.alignment = PAGE_ALIGNMENT;
                info.size = reqs.size;
                VmaVirtualAllocation vmaalloc{};
                size_t offset{};
                vmaVirtualAllocate(p.block, &info, &vmaalloc, &offset);
                if(!vmaalloc) { continue; }
                Allocation alloc{};
                alloc.alloc = vmaalloc;
                alloc.page = &p;
                alloc.size = reqs.size;
                alloc.offset = offset;
                allocations.push_back(alloc);
                return &allocations.back();
            }

            auto preqs = reqs;
            preqs.size = PAGE_SIZE;
            preqs.alignment = PAGE_ALIGNMENT;
            Page p{};
            p.memory = allocator(preqs);
            VmaVirtualBlockCreateInfo vmainfo{};
            vmainfo.size = preqs.size;
            vmaCreateVirtualBlock(&vmainfo, &p.block);
            if(!p.block)
            {
                ENG_ASSERT(false);
                return nullptr;
            }
            pages.push_front(p);
        }
    }

    void free(void* const alloc)
    {
        auto* a = (Allocation*)alloc;
        if(!a || !a->alloc) { return; }
        ENG_ASSERT(a->page);
        vmaVirtualFree(a->page->block, a->alloc);
        *a = Allocation{};
    }

    void get_offset_and_base(const void* alloc, void*& base, size_t& offset)
    {
        ENG_ASSERT(alloc);
        auto* a = (Allocation*)alloc;
        base = a->page->memory;
        offset = a->offset;
    }

    void reset_pages()
    {
        for(auto& p : pages)
        {
            vmaClearVirtualBlock(p.block);
        }
        allocations.clear();
    }

    Callback<void*(const RendererMemoryRequirements&)> allocator;
    std::deque<Page> pages;
    std::deque<Allocation> allocations;
};

RGResourceId RGBuilder::add_resource(const RGResource& resource, const std::optional<RGClear>& clear)
{
    if(graph->resources.size() >= RGRESOURCEID_PERSISTENT_BIT)
    {
        ENG_ASSERT(false, "Too many resources");
        return RGResourceId{};
    }

    ENG_ASSERT(!clear);
    auto layout = resource.is_buffer() ? ImageLayout::UNDEFINED : resource.as_image()->layout;
    RGWaitSync* wait_sync{};
    RGResourceId resource_id{ (u32)graph->resources.size() };

    if(resource.is_persistent())
    {
        auto& p = *resource.persistent;
        // i don't think the if is needed, since it's gonna be undefined anyway
        // if(!resource.is_buffer() && it->second.last_layout != ImageLayout::UNDEFINED){}
        layout = p.last_layout;
        if(p.wait_sync.sync) { wait_sync = &p.wait_sync; }
        resource_id = p.id;
    }
    else { graph->resources.push_back(resource); }

    add_access(RGAccess{
        .resource = resource_id, .prev_access = {}, .buffer_view = {}, .layout = layout, .stage = {}, .access = {}, .wait_sync = wait_sync });
    return resource_id;
}

RGResourceId RGBuilder::import_resource(RGResourceId id, DiscardContents discard, const std::optional<RGClear>& clear)
{
    if(!RGRenderGraph::is_persistent(id))
    {
        ENG_WARN("Trying to import non persistent resource {}", *id);
        return RGResourceId{};
    }
    const auto idx = RGRenderGraph::extract_idx(id);
    if(graph->persistent_resources.size() <= idx)
    {
        ENG_WARN("Index out of range {}", *id);
        return RGResourceId{};
    }
    return add_resource(graph->persistent_resources[idx].resource, clear);
}

RGResourceId RGBuilder::import_resource(const RGResource::NativeResource& resource, DiscardContents discard,
                                        const std::optional<RGClear>& clear)
{
    if((resource.index() == 0 && !std::get<0>(resource)) || (resource.index() == 1 && !std::get<1>(resource)))
    {
        static_assert(std::variant_size_v<RGResource::NativeResource> == 2);
        return {};
    }

    const auto it = std::find_if(graph->resources.begin(), graph->resources.end(),
                                 [&resource](const auto& e) { return e.native == resource; });
    if(it != graph->resources.end()) { return RGResourceId{ (u32)std::distance(graph->resources.begin(), it) }; }

    auto res = RGResource{ "", resource, nullptr, false, true, clear };
    if(res.is_buffer()) { res.name = get_backend().get_debug_name(res.as_buffer().get()); }
    else { res.name = get_backend().get_debug_name(res.as_image().get()); }

    return add_resource(std::move(res));
}

RGResourceId RGBuilder::create_resource(std::string_view name, RGNativeResourceVariant&& a,
                                        const std::optional<RGClear>& clear, bool is_persistent)
{
    ENG_ASSERT(!name.empty());
    const bool is_aliased = !is_persistent && !graph->memory_aliasing_disabled;

    RGResource::NativeResource native;
    const hash_t name_hash = ENG_HASH(name);
    const hash_t object_hash = ENG_HASH(a);
    const AllocateMemory allocate_memory = is_aliased ? AllocateMemory::ALIASED : AllocateMemory::YES;
    RGPersistentResource* found_persistent{};

    if(!is_aliased)
    {
        auto prit = std::find_if(graph->persistent_resources.begin(), graph->persistent_resources.end(),
                                 [name_hash](const RGPersistentResource& pr) { return pr.name_hash == name_hash; });
        if(prit != graph->persistent_resources.end())
        {
            found_persistent = &*prit;
            // check if object hash changed (for example resolution might have changed)
            if(prit->object_hash != object_hash) { get_renderer().queue_destroy(prit->resource.native); }
            else
            {
                auto res = prit->resource;
                res.clear = clear;
                return add_resource(res);
            }
        }
    }

    if(std::get_if<0>(&a)) { native = get_renderer().make_buffer(name, std::move(std::get<0>(a)), allocate_memory); }
    else if(std::get_if<1>(&a))
    {
        native = get_renderer().make_image(name, std::move(std::get<1>(a)), allocate_memory);
    }
    else
    {
        ENG_ASSERT(false);
        return RGResourceId{};
    }

    RGResource resource{ name, native, nullptr, is_aliased, is_persistent, clear };
    if(is_persistent)
    {
        if(found_persistent == nullptr)
        {
            found_persistent = &graph->persistent_resources.emplace_back();
            found_persistent->id = RGResourceId{ ((u32)graph->persistent_resources.size() - 1) | RGRESOURCEID_PERSISTENT_BIT };
        }
        found_persistent->name_hash = name_hash;
        found_persistent->object_hash = object_hash;
        resource.persistent = found_persistent;
        found_persistent->resource = resource;
    }

    return add_resource(resource);
}

RGResourceId RGBuilder::create_resource(std::string_view name, Buffer&& a, bool is_persistent)
{
    return create_resource(name, RGNativeResourceVariant{ std::in_place_index<0>, std::move(a) }, {}, is_persistent);
}

RGResourceId RGBuilder::create_resource(std::string_view name, Image&& a, const std::optional<RGClear>& clear, bool is_persistent)
{
    return create_resource(name, RGNativeResourceVariant{ std::in_place_index<1>, std::move(a) }, clear, is_persistent);
}

void RGBuilder::add_access(const RGAccess& a)
{
    if(auto it = pass->res_to_acc.find(a.resource); it != pass->res_to_acc.end() && !get_acc(it->second).is_first_access())
    {
        ENG_WARN("Pass \"{}\" already references this resource (\"{}\")", pass->name.as_view(),
                 graph->get_res(a.resource).name.as_view());
        return;
    }

    const auto accid = RGAccessId{ (u32)graph->accesses.size() };
    graph->accesses.push_back(a);
    pass->res_to_acc[a.resource] = accid;
    graph->get_res(a.resource).last_access = accid;
    pass->stage_mask |= a.stage;
}

RGResourceId RGBuilder::access_resource(RGResourceId res, ImageLayout layout, Flags<PipelineStage> stage,
                                        Flags<PipelineAccess> access, std::optional<ImageFormat> format,
                                        std::optional<ImageViewType> type, Range32u mips, Range32u layers)
{
    if(!res)
    {
        ENG_WARN("Accessing invalid resource");
        return RGResourceId{};
    }
    add_access(RGAccess{
        .resource = res,
        .prev_access = graph->get_res(res).last_access,
        .image_view = ImageView::init(graph->get_img(res), format, type, mips.offset, mips.size, layers.offset, layers.size),
        .layout = layout,
        .stage = stage,
        .access = access,
    });
    return res;
}

RGResourceId RGBuilder::access_resource(RGResourceId res, Flags<PipelineStage> stage, Flags<PipelineAccess> access, Range64u range)
{
    if(!res)
    {
        ENG_WARN("Accessing invalid resource");
        return RGResourceId{};
    }
    add_access(RGAccess{
        .resource = res,
        .prev_access = graph->get_res(res).last_access,
        .buffer_view = BufferView::init(graph->get_buf(res), range.offset, range.size),
        .stage = stage,
        .access = access,
    });
    return res;
}

const RGAccess& RGBuilder::get_acc(RGAccessId acc) const
{
    ENG_ASSERT(acc);
    return graph->get_acc(acc);
}

const RGAccess& RGBuilder::get_acc(RGResourceId res) const
{
    ENG_ASSERT(res);
    return get_acc(pass->res_to_acc.at(res));
}

const BufferView& RGBuilder::get_buf(RGAccessId acc) const { return get_acc(acc).buffer_view; }

const BufferView& RGBuilder::get_buf(RGResourceId acc) const { return get_acc(acc).buffer_view; }

const ImageView& RGBuilder::get_img(RGAccessId acc) const { return get_acc(acc).image_view; }

const ImageView& RGBuilder::get_img(RGResourceId acc) const { return get_acc(acc).image_view; }

ICommandBuffer* RGBuilder::open_cmd_buf()
{
    ENG_ASSERT(pass->cmd == nullptr);
    pass->cmd = graph->cmd_pools[0]->begin();
    pass->cmd->begin_label(pass->name.c_str());
    pass->query = &get_renderer().current_data->tstamp_queries.emplace_back();
    pass->query->pool = get_renderer().current_data->timestamp_pool;
    pass->query->label = pass->name;
    pass->query->index = get_renderer().current_data->timestamp_pool->allocate_queries(2);
    pass->cmd->reset_query_indices(pass->query->pool, pass->query->index, 2);
    pass->cmd->write_timestamp(pass->query->pool, PipelineStage::ALL, pass->query->index);
    gfx::set_debug_name(((CommandBufferVk*)pass->cmd)->cmd, pass->name.as_view());
    return pass->cmd;
}

void RGRenderGraph::init(Renderer* r)
{
    queue = r->get_queue(QueueType::GRAPHICS);
    sems[0] = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync 0" });
    sems[1] = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync 1" });
    cmd_pools[0] = queue->make_command_pool();
    cmd_pools[1] = queue->make_command_pool();
    const auto allocate_aliasable = [r](const RendererMemoryRequirements& reqs) {
        return r->backend->allocate_aliasable_memory(reqs);
    };
    allocators[0] = new GPUTransientAllocator{ allocate_aliasable };
    allocators[1] = new GPUTransientAllocator{ allocate_aliasable };
    allocator = allocators[0];
    m_debug_datas_arr[0] = new RGDebugData{};
    m_debug_datas_arr[1] = new RGDebugData{};
}

void RGRenderGraph::compile()
{
    std::swap(allocators[0], allocators[1]);
    std::swap(m_debug_datas_arr[0], m_debug_datas_arr[1]);
    allocator = allocators[0];
    allocator->reset_pages();

    const auto group_passes = [this] {
        const auto get_earliest_group_for_pass = [this](const std::map<RGResourceId, RGAccessId>& accesses) -> u32 {
            return std::accumulate(accesses.begin(), accesses.end(), 0u, [this](auto max, const auto& rg_acc_pair) {
                return std::max(max, [this, &rg_acc_pair] {
                    const auto& acc = get_acc(rg_acc_pair.second);
                    // access just created the resource, we can start right away.
                    if(acc.is_first_access()) { return 0u; }
                    const auto& res = get_res(rg_acc_pair.second);
                    // if current access is a write, we need to wait for previous reads and writes.
                    if(acc.is_write() || passes_serialized)
                    {
                        return std::max(res.last_read_group + 1, res.last_write_group + 1);
                    }
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
        const auto update_resource_accesses = [this](const std::map<RGResourceId, RGAccessId>& accesses, u32 last_group) {
            std::for_each(accesses.begin(), accesses.end(), [this, last_group](const auto& res_acc_pair) {
                const auto& acc = get_acc(res_acc_pair.second);
                auto& res = get_res(res_acc_pair.second);
                if(acc.is_read()) { res.last_read_group = last_group; }
                if(acc.is_write()) { res.last_write_group = last_group; }
            });
        };
        groups.clear();
        groups.resize(passes.size());
        u32 last_gid = 0;
        for(auto& p : passes)
        {
            const auto gid = get_earliest_group_for_pass(p->res_to_acc);
            last_gid = std::max(last_gid, gid);
            groups[gid].passes.push_back(&*p);
            update_resource_accesses(p->res_to_acc, gid);
        }
        groups.resize(last_gid + 1);
    };
    const auto bind_aliased_memory_to_resources = [this] {
        // Allocate memory from transient allocator for resources to be used during execution
        std::set<RGResourceId> alive_res_set;
        std::set<RGResourceId> res_to_remove;
        const auto free_transient_mem_from_dead_res_in_prev_group = [this, &res_to_remove, &alive_res_set] {
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
            free_transient_mem_from_dead_res_in_prev_group();
            for(auto* p : g.passes)
            {
                for(const auto& [rr, ra] : p->res_to_acc)
                {
                    auto& acc = get_acc(ra);
                    auto& res = get_res(ra);
                    if(!res.is_aliased) { continue; }
                    auto insertion = alive_res_set.insert(acc.resource);
                    if(res.last_access == ra) { res_to_remove.insert(acc.resource); }
                    if(!insertion.second) { continue; }
                    // insertion happened, this resource has never had memory bound to it during this frame
                    RendererMemoryRequirements reqs;
                    void* alloc;
                    void* base;
                    size_t offset;
                    if(res.is_buffer())
                    {
                        get_backend().get_memory_requirements(res.as_buffer().get(), reqs);
                        alloc = allocator->allocate(reqs);
                        allocator->get_offset_and_base(alloc, base, offset);
                        get_backend().bind_aliasable_memory(res.as_buffer().get(), base, offset);
                    }
                    else
                    {
                        get_backend().get_memory_requirements(res.as_image().get(), reqs);
                        alloc = allocator->allocate(reqs);
                        allocator->get_offset_and_base(alloc, base, offset);
                        get_backend().bind_aliasable_memory(res.as_image().get(), base, offset);
                    }
                    res.alloc = alloc;
                }
            }
        }
        free_transient_mem_from_dead_res_in_prev_group();
    };

    group_passes();
    bind_aliased_memory_to_resources();
    m_debug_datas_arr[0]->build(this);
}

Sync* RGRenderGraph::execute(Sync** wait_syncs, u32 wait_count)
{
    std::swap(sems[0], sems[1]);
    std::swap(cmd_pools[0], cmd_pools[1]);
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
        const auto consume_wait_sync = [this, &layout_cmd](const RGPass* p, RGAccess& acc) {
            auto* sync = acc.wait_sync;
            ENG_ASSERT(sync);
            if(!sync) { return; }
            // return, because if sems are the same, it means frame_delay had passed and renderer waited for the fence
            if(sync->sync == sems[0]) { return; }
            if(!layout_cmd) { layout_cmd = cmd_pools[0]->begin(); }
            // ENG_LOG("Waiting on a sync {} for resource {} in pass {}", sync->sync->name, p->name.as_view(),
            //         get_res(acc.resource).name.as_view());
            layout_cmd->wait_sync(sync->sync, sync->wait_value, acc.stage);
        };

        for(const auto* p : g.passes)
        {
            gstages |= p->stage_mask;
            for(const auto [pr, pa] : p->res_to_acc)
            {
                auto& acc = get_acc(pa);
                const auto& res = get_res(pa);
                if(acc.is_first_access())
                {
                    if(acc.wait_sync) { consume_wait_sync(p, acc); }

                    if(res.is_buffer()) { continue; }

                    if(res.clear)
                    {
                        if(!layout_cmd) { layout_cmd = cmd_pools[0]->begin(); }

                        acc.stage = PipelineStage::TRANSFER_BIT;
                        acc.access = PipelineAccess::TRANSFER_WRITE_BIT;
                        acc.layout = ImageLayout::TRANSFER_DST;
                        layout_cmd->barrier(res.as_image().get(), PipelineStage::NONE, PipelineAccess::NONE,
                                            PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                                            ImageLayout::UNDEFINED, ImageLayout::TRANSFER_DST);
                        if(res.clear->is_color())
                        {
                            const auto clear = res.clear->get_color().color;
                            layout_cmd->clear_color(res.as_image().get(), f32_4{ clear.x, clear.y, clear.z, clear.w });
                        }
                        else
                        {
                            const auto clear = res.clear->get_ds();
                            layout_cmd->clear_depth_stencil(res.as_image().get(), clear.depth, clear.stencil);
                        }
                    }
                    continue;
                }

                const auto& pacc = get_acc(acc.prev_access);
                ENG_ASSERT((!pacc.is_first_access() && !pacc.wait_sync) || (pacc.is_first_access())); // only first access may store wait_sync

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
            // queue->submit(); // this need not be submitted individually
        }

        for(auto* p : g.passes)
        {
            RGBuilder pb{ p, this };
            p->execute(pb);
            if(!p->cmd) { continue; }
            p->cmd->end_label();
            p->cmd->write_timestamp(p->query->pool, PipelineStage::ALL, p->query->index + 1);
            cmd_pools[0]->end(p->cmd);
            queue->with_cmd_buf(p->cmd);
            for(const auto& [res_id, acc_id] : p->res_to_acc)
            {
                auto& acc = get_acc(acc_id);
                auto& res = get_res(acc_id);
                if(res.last_access == acc_id)
                {
                    if(!res.is_external) { queue_destroy_resource(res); }
                    else if(res.is_persistent())
                    {
                        auto& pr = *res.persistent;
                        pr.last_layout = acc.layout;
                        if(res.last_write_group != ~0u)
                        {
                            pr.wait_sync = { sems[0], sems[0]->get_current_wait_value() };
                        }
                    }
                }
            }
        }
        queue->wait_sync(sems[0], gstages);
        queue->signal_sync(sems[0], gstages);
        queue->submit();
    }

    for(auto& pr : persistent_resources)
    {
        pr.resource.last_access = {};
        pr.resource.last_read_group = ~0u;
        pr.resource.last_write_group = ~0u;
        pr.resource.alloc = nullptr;
    }

    resources.clear();
    accesses.clear();
    passes.clear();
    // namedpasses.clear();

    return sems[0];
}

void RGRenderGraph::queue_destroy_resource(RGResource& res)
{
    ENG_ASSERT(!res.is_persistent());
    auto& r = get_renderer();
    r.queue_destroy(res.native);
}

void RGDebugData::build(RGRenderGraph* rg)
{
    clear();
    for(const auto& r : rg->resources)
    {
        auto& dr = resources.emplace_back();
        dr.name = r.name.c_str();
        // if(r.is_buffer()) { dr.resource = r.as_buffer().get(); }
        // else { dr.resource = r.as_image().get(); }
        dr.persistent = r.is_persistent();
        dr.aliased_memory = r.is_aliased;
    }
    for(const auto& g : rg->groups)
    {
        auto& dg = groups.emplace_back();
        for(const auto& p : g.passes)
        {
            auto& dp = dg.passes.emplace_back(Pass{ p->name.c_str(), {}, p->query });
            for(const auto& [pr, pa] : p->res_to_acc)
            {
                const auto& pacc = rg->get_acc(pa);
                const auto& pres = rg->get_res(pa);
                dp.accesses.push_back(Access{ *pacc.resource, pacc.stage, pacc.access, pacc.layout, pres.last_access == pa });
            }
        }
    }
}

} // namespace gfx
} // namespace eng