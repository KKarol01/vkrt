#include "rendergraph.hpp"

#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/math/align.hpp>
#include <eng/renderer/vulkan/vulkan_backend.hpp>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <eng/common/handle.hpp>
#include <eng/engine.hpp>
#include <eng/renderer/set_debug_name.hpp>

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

RGAccessId RGBuilder::add_resource(const RGResource& resource, const std::optional<RGClear>& clear)
{
    ENG_ASSERT(!clear);
    auto layout = resource.is_buffer() ? ImageLayout::UNDEFINED : resource.as_image()->layout;
    RGWaitSync* wait_sync{};
    if(auto it = graph->persistent_resources.find(ENG_HASH(resource.name)); it != graph->persistent_resources.end())
    {
        if(!resource.is_buffer() && it->second.last_layout != ImageLayout::UNDEFINED)
        {
            layout = it->second.last_layout;
        }
        if(it->second.wait_sync.sync) { wait_sync = &it->second.wait_sync; }
    }

    graph->resources.push_back(resource);
    const auto ret = add_access(RGAccess{ .resource = RGResourceId{ (u32)graph->resources.size() - 1 },
                                          .prev_access = {},
                                          .buffer_view = {},
                                          .layout = layout,
                                          .stage = {},
                                          .access = {},
                                          .wait_sync = wait_sync });
    return ret;
}

RGAccessId RGBuilder::import_resource(const RGResource::NativeResource& resource, const std::optional<RGClear>& clear)
{
    if((resource.index() == 0 && !std::get<0>(resource)) || (resource.index() == 1 && !std::get<1>(resource)))
    {
        static_assert(std::variant_size_v<RGResource::NativeResource> == 2);
        return {};
    }

    const auto it = std::find_if(graph->resources.begin(), graph->resources.end(),
                                 [&resource](const auto& e) { return e.native == resource; });
    if(it != graph->resources.end()) { return it->last_access; }

    auto res = RGResource{ "", resource, true, false, clear };
    if(res.is_buffer()) { res.name = get_renderer().backend->get_debug_name(res.as_buffer().get()); }
    else { res.name = get_renderer().backend->get_debug_name(res.as_image().get()); }

    const auto hash = ENG_HASH(res.name);
    auto& pr = graph->persistent_resources[hash]; // make sure it exists for execute()
    pr.hash = 0;                                  // imported resources don't need a hash.
    pr.native = resource;

    return add_resource(std::move(res));
}

RGAccessId RGBuilder::create_resource(std::string_view name, Buffer&& a, bool persistent)
{
    ENG_ASSERT(!name.empty());
    const bool is_aliased = !persistent && !graph->memory_aliasing_disabled;
    RGResource::NativeResource native = [this, &a, &name, persistent, is_aliased]() -> RGResource::NativeResource {
        if(!is_aliased)
        {
            const auto namehash = ENG_HASH(name);
            const auto objecthash = ENG_HASH(a);
            if(auto it = graph->persistent_resources.find(namehash); it != graph->persistent_resources.end())
            {
                if(it->second.hash != objecthash)
                {
                    // object has changed -- for example image resolution might have changed -- recreate
                    get_renderer().queue_destroy(*std::get_if<Handle<Buffer>>(&it->second.native));
                    graph->persistent_resources.erase(it);
                }
                else { return it->second.native; }
            }
            auto native_handle = get_renderer().make_buffer(name, std::move(a));
            if(persistent)
            {
                auto res = graph->persistent_resources.emplace(std::piecewise_construct, std::forward_as_tuple(namehash),
                                                               std::forward_as_tuple(objecthash, native_handle));
                ENG_ASSERT(res.second, "Name conflict with resource {} in pass {}", name, pass->name.as_view());
            }
            return native_handle;
        }
        const AllocateMemory allocation_mode = is_aliased ? AllocateMemory::ALIASED : AllocateMemory::YES;
        return get_renderer().make_buffer(name, std::move(a), allocation_mode);
    }();
    return add_resource(RGResource{ name, native, persistent, is_aliased });
}

RGAccessId RGBuilder::create_resource(std::string_view name, Image&& a, const std::optional<RGClear>& clear, bool persistent)
{
    ENG_ASSERT(!name.empty());
    const bool is_aliased = !persistent && !graph->memory_aliasing_disabled;
    RGResource::NativeResource native = [this, &a, &name, persistent, is_aliased]() -> RGResource::NativeResource {
        if(!is_aliased)
        {
            const auto namehash = ENG_HASH(name);
            const auto objecthash = ENG_HASH(a);
            if(auto it = graph->persistent_resources.find(namehash); it != graph->persistent_resources.end())
            {
                if(it->second.hash != objecthash)
                {
                    // object has changed -- for example image resolution might have changed -- recreate
                    get_renderer().queue_destroy(*std::get_if<Handle<Image>>(&it->second.native));
                    graph->persistent_resources.erase(it);
                }
                else { return it->second.native; }
            }
            auto native_handle = get_renderer().make_image(name, std::move(a));
            if(persistent)
            {
                graph->persistent_resources.emplace(std::piecewise_construct, std::forward_as_tuple(namehash),
                                                    std::forward_as_tuple(objecthash, native_handle));
            }
            return native_handle;
        }
        const AllocateMemory allocation_mode = is_aliased ? AllocateMemory::ALIASED : AllocateMemory::YES;
        return get_renderer().make_image(name, std::move(a), allocation_mode);
    }();
    return add_resource(RGResource{ name, native, persistent, is_aliased, clear });
}

RGAccessId RGBuilder::add_access(const RGAccess& a)
{
    const bool already_contains_resource = std::any_of(pass->accesses.begin(), pass->accesses.end(), [this, &a](RGAccessId id) {
        auto& acc = graph->get_acc(id);
        return !acc.is_first_access() && acc.resource == a.resource;
    });
    if(already_contains_resource)
    {
        ENG_WARN("Pass \"{}\" already references this resource (\"{}\")", pass->name.as_view(),
                 graph->get_res(a.resource).name.as_view());
        return {};
    }

    graph->accesses.push_back(a);
    const auto ret = RGAccessId{ (u32)graph->accesses.size() - 1 };
    graph->get_res(ret).last_access = ret;
    pass->accesses.push_back(ret);
    pass->stage_mask |= a.stage;
    return ret;
}

RGAccessId RGBuilder::access_resource(RGAccessId acc, ImageLayout layout, Flags<PipelineStage> stage,
                                      Flags<PipelineAccess> access, std::optional<ImageFormat> format,
                                      std::optional<ImageViewType> type, Range32u mips, Range32u layers)
{
    if(!acc) { return RGAccessId{}; }
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
    if(!acc) { return RGAccessId{}; }
    return add_access(RGAccess{
        .resource = graph->get_acc(acc).resource,
        .prev_access = acc,
        .buffer_view = BufferView::init(graph->get_buf(acc), range.offset, range.size),
        .stage = stage,
        .access = access,
    });
}

RGResourceId RGBuilder::as_res_id(RGAccessId acc) const
{
    if(!acc) { return {}; }
    return graph->get_acc(acc).resource;
}

RGAccessId RGBuilder::as_acc_id(RGResourceId res) const
{
    if(!res) { return {}; }
    return graph->get_res(res).last_access;
}

const RGAccess& RGBuilder::get_acc(RGAccessId acc) const
{
    ENG_ASSERT(acc);
    return graph->get_acc(acc);
}

const RGAccess& RGBuilder::get_acc(RGResourceId res) const
{
    ENG_ASSERT(res);
    return graph->get_acc(res);
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
        const auto get_earliest_group_for_pass = [this](const std::vector<RGAccessId>& accesses) -> u32 {
            return std::accumulate(accesses.begin(), accesses.end(), 0u, [this](auto max, const auto& val) {
                return std::max(max, [this, &val] {
                    const auto& acc = get_acc(val);
                    // access just created the resource, we can start right away.
                    if(acc.is_first_access()) { return 0u; }
                    const auto& res = get_res(val);
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
        const auto update_resource_accesses = [this](const std::vector<RGAccessId>& accesses, u32 last_group) {
            std::for_each(accesses.begin(), accesses.end(), [this, last_group](RGAccessId a) {
                const auto& acc = get_acc(a);
                auto& res = get_res(a);
                if(acc.is_read()) { res.last_read_group = last_group; }
                if(acc.is_write()) { res.last_write_group = last_group; }
            });
        };
        groups.clear();
        groups.resize(passes.size());
        u32 last_gid = 0;
        for(auto& p : passes)
        {
            const auto gid = get_earliest_group_for_pass(p->accesses);
            last_gid = std::max(last_gid, gid);
            groups[gid].passes.push_back(&*p);
            update_resource_accesses(p->accesses, gid);
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
                    if(!res.is_aliased) { continue; }
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
            for(const auto pa : p->accesses)
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
                            layout_cmd->clear_color(res.as_image().get(), Color4f{ clear.x, clear.y, clear.z, clear.w });
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
            for(auto accid : p->accesses)
            {
                auto& acc = get_acc(accid);
                auto& res = get_res(accid);
                // todo: maybe pool handles here? renderer already does that, soo...
                // also, maybe add resource set in pass, because iterating over accesses is going over same resource multiple times (potentially)
                if(res.last_access == accid)
                {
                    if(!res.is_persistent) { free_resource(res); }
                    else
                    {
                        if(auto it = persistent_resources.find(ENG_HASH(res.name)); it != persistent_resources.end())
                        {
                            auto& pr = it->second;
                            pr.last_layout = acc.layout;
                            if(res.last_write_group != ~0u)
                            {
                                // only add for resources that are not read-only
                                pr.wait_sync = { sems[0], sems[0]->get_current_wait_value() };
                            }
                        }
                    }
                }
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

void RGRenderGraph::free_resource(RGResource& res)
{
    ENG_ASSERT(!res.is_persistent);
    auto& r = get_renderer();
    if(res.is_buffer())
    {
        auto h = res.as_buffer();
        r.queue_destroy(h);
    }
    else
    {
        auto h = res.as_image();
        r.queue_destroy(h);
    }
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
        dr.persistent = r.is_persistent;
        dr.aliased_memory = r.is_aliased;
    }
    for(const auto& g : rg->groups)
    {
        auto& dg = groups.emplace_back();
        for(const auto& p : g.passes)
        {
            auto& dp = dg.passes.emplace_back(Pass{ p->name.c_str(), {}, p->query });
            for(const auto& pa : p->accesses)
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