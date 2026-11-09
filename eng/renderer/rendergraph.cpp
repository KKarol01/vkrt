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

namespace
{

struct RGBarrier
{
    bool needed{};
    Flags<PipelineStage> src_stage{};
    Flags<PipelineAccess> src_access{};
    ImageLayout old_layout{ ImageLayout::UNDEFINED };
};

RGBarrier update_state_and_produce_barrier(RGResourceState& s, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                                           ImageLayout layout, bool is_image, bool discard)
{
    const bool is_write = (access & PipelineAccess::WRITES).any();
    const bool is_read = (access & PipelineAccess::READS).any();
    const bool layout_change = is_image && (discard || s.layout != layout);
    RGBarrier b{};
    b.old_layout = discard ? ImageLayout::UNDEFINED : s.layout;

    if(layout_change || is_write)
    {
        b.src_stage = s.write_stages | s.read_stages; // wait for all the reads and writes first
        b.src_access = s.write_access;                // flush writes and invalidate caches
        b.needed = layout_change || b.src_stage.any();
        if(is_image) { s.layout = layout; }
        s.read_stages = is_read ? stage : Flags<PipelineStage>{};
        if(is_write)
        {
            s.write_stages = stage;
            s.write_access = access & PipelineAccess::WRITES;
            s.read_stages_accum = {};
            s.read_accesses_accum = {};
        }
        // pure layout transition branch
        else
        {
            s.write_stages = stage;
            s.write_access = {};
            s.read_stages_accum = stage;
            s.read_accesses_accum = access;
        }
        return b;
    }

    if(is_read)
    {
        const bool barrier_already_covers_that_read =
            (stage & ~s.read_stages_accum).empty() && (access & ~s.read_accesses_accum).empty();
        // if we need to synchronise with any writes and if this is the first time we're doing so for that stage+access in this read chain since last write
        if(s.write_stages.any() && !barrier_already_covers_that_read)
        {
            b.needed = true;
            b.src_stage = s.write_stages;
            b.src_access = s.write_access;
            s.read_stages_accum |= stage;
            s.read_accesses_accum |= access;
        }
        s.read_stages |= stage;
    }
    return b;
}

void record_barrier_if_needed(auto&& get_cmd, RGResource& res, const RGAccess& acc, bool discard)
{
    if(acc.stage.empty() && acc.access.empty()) { return; }

    const bool is_image = !res.is_buffer();
    const auto b = update_state_and_produce_barrier(res.state, acc.stage, acc.access,
                                                    is_image ? acc.layout : ImageLayout::UNDEFINED, is_image, discard);
    if(!b.needed) { return; }
    if(is_image)
    {
        get_cmd()->barrier(res.as_image().get(), b.src_stage, b.src_access, acc.stage, acc.access, b.old_layout, acc.layout);
    }
    else { get_cmd()->barrier(b.src_stage, b.src_access, acc.stage, acc.access); }
}

void get_or_init_resource_state(RGRenderGraph& graph, RGResource& res)
{
    if(res.is_persistent)
    {
        auto it = graph.persistent_states_map.find(RGResource::make_key(res.native));
        if(it != graph.persistent_states_map.end())
        {
            res.state = it->second;
            return;
        }
    }
    RGResourceState s{};
    if(!res.is_buffer()) { s.layout = res.as_image()->layout; }
    if(res.is_aliased())
    {
        // may have been used earlier by previous resource that aliased -- wait
        s.layout = ImageLayout::UNDEFINED;
        s.write_stages = PipelineStage::ALL;
        s.write_access = PipelineAccess::WRITES;
    }
    res.state = s;
}

void make_access_a_clear(RGAccess& acc, const RGResource& res, const RGClear& clear)
{
    if(res.is_buffer())
    {
        ENG_ERROR("Clearing buffers is not supported (\"{}\"); ignoring the clear.", res.name.as_view());
        acc = RGAccess{};
        return;
    }
    acc.image_view = ImageView::init(res.as_image());
    acc.layout = ImageLayout::TRANSFER_DST;
    acc.stage = PipelineStage::TRANSFER_BIT;
    acc.access = PipelineAccess::TRANSFER_WRITE_BIT;
    acc.clear = clear;
}

// Finds or (re)creates the copies of a persistent() resource and returns the one to use this frame.
template <typename Desc, typename MakeFunc>
RGResource::NativeResource acquire_persistent(RGRenderGraph& graph, std::string_view name, const Desc& desc,
                                              u32 history, MakeFunc&& make)
{
    history = std::max(history, 1u);
    const auto deschash = ENG_HASH(desc);
    RGPersistentResource& pr = graph.persistent_resources[ENG_HASH(name)];
    ENG_ASSERT(std::none_of(graph.resources.begin(), graph.resources.end(),
                            [h = ENG_HASH(name)](const RGResource& r) { return r.name_hash == h; }),
               "Persistent resource \"{}\" was requested twice in one frame.", name);
    if(pr.hash != deschash || pr.copies.size() != history)
    {
        for(const auto& c : pr.copies)
        {
            graph.persistent_states_map.erase(RGResource::make_key(c));
            std::visit([](auto h) { get_renderer().queue_destroy(h); }, c);
        }
        pr.copies.clear();
        for(u32 i = 0; i < history; ++i)
        {
            const auto copy_name = history == 1 ? std::string{ name } : ENG_FMT("{} {}", name, i);
            pr.copies.push_back(make(std::string_view{ copy_name }, Desc{ desc }));
        }
        pr.hash = deschash;
    }
    return pr.copies[graph.frame % history];
}

} // namespace

u32 RGBuilder::add_resource(const RGResource& resource, const std::optional<RGClear>& clear)
{
    graph->resources.push_back(resource);
    const u32 index = (u32)graph->resources.size() - 1;
    const auto& res = graph->resources.back();
    RGAccess acc{ .resource = index, .is_first = true, .buffer_view = {} };
    // the first access is a no-op (no stages), unless it clears; give it a valid view either way
    if(res.is_buffer()) { acc.buffer_view = BufferView::init(res.as_buffer()); }
    else { acc.image_view = ImageView::init(res.as_image()); }
    if(clear) { make_access_a_clear(acc, res, *clear); }
    add_access(acc);
    return index;
}

u32 RGBuilder::import_native(const RGResource::NativeResource& native, const std::optional<RGClear>& clear)
{
    const auto it = std::find_if(graph->resources.begin(), graph->resources.end(),
                                 [&native](const auto& e) { return e.native == native; });
    if(it != graph->resources.end())
    {
        const auto index = (u32)std::distance(graph->resources.begin(), it);
        ENG_ASSERT(std::holds_alternative<Handle<Image>>(native));
        if(clear)
        {
            RGAccess acc{ .resource = index, .buffer_view = {} };
            make_access_a_clear(acc, *it, *clear);
            add_access(acc);
        }
        return index;
    }

    // imported resources are persistent: their state is kept between frames, keyed by the handle
    auto res = RGResource{ "", native, true };
    if(res.is_buffer()) { res.name = get_renderer().backend->get_debug_name(res.as_buffer().get()); }
    else { res.name = get_renderer().backend->get_debug_name(res.as_image().get()); }
    return add_resource(res, clear);
}

RGImage RGBuilder::import_resource(Handle<Image> image, const std::optional<RGClear>& clear)
{
    if(!image) { return {}; }
    return RGImage{ import_native(image, clear) };
}

RGBuffer RGBuilder::import_resource(Handle<Buffer> buffer)
{
    if(!buffer) { return {}; }
    return RGBuffer{ import_native(buffer, {}) };
}

RGImage RGBuilder::create_resource(std::string_view name, Image&& desc, const std::optional<RGClear>& clear)
{
    ENG_ASSERT(!name.empty());
    const auto image = get_renderer().make_image(name, std::move(desc), AllocateMemory::ALIASED);
    return RGImage{ add_resource(RGResource{ name, image, false }, clear) };
}

RGBuffer RGBuilder::create_resource(std::string_view name, Buffer&& desc)
{
    ENG_ASSERT(!name.empty());
    const auto buffer = get_renderer().make_buffer(name, std::move(desc), AllocateMemory::ALIASED);
    return RGBuffer{ add_resource(RGResource{ name, buffer, false }) };
}

RGImage RGBuilder::persistent(std::string_view name, Image&& desc, const std::optional<RGClear>& clear, u32 history)
{
    ENG_ASSERT(!name.empty());
    const auto native = acquire_persistent(*graph, name, desc, history, [](std::string_view n, Image&& d) {
        return RGResource::NativeResource{ get_renderer().make_image(n, std::move(d)) };
    });
    return RGImage{ add_resource(RGResource{ name, native, true }, clear) };
}

RGBuffer RGBuilder::persistent(std::string_view name, Buffer&& desc, u32 history)
{
    ENG_ASSERT(!name.empty());
    const auto native = acquire_persistent(*graph, name, desc, history, [](std::string_view n, Buffer&& d) {
        return RGResource::NativeResource{ get_renderer().make_buffer(n, std::move(d)) };
    });
    return RGBuffer{ add_resource(RGResource{ name, native, true }) };
}

namespace
{
const RGResource::NativeResource* get_previous_copy(RGRenderGraph& graph, const RGResource& res)
{
    ENG_ASSERT(res.name_hash, "previous() needs a resource created with persistent(), got \"{}\".", res.name.as_view());
    const auto it = graph.persistent_resources.find(res.name_hash);
    if(it == graph.persistent_resources.end()) { return nullptr; }
    const auto& pr = it->second;
    return &pr.copies[(graph.frame + pr.copies.size() - 1) % pr.copies.size()];
}
} // namespace

RGImage RGBuilder::previous(RGImage image)
{
    if(!image) { return {}; }
    const auto* native = get_previous_copy(*graph, graph->get_res(image));
    if(!native) { return {}; }
    return RGImage{ import_native(*native, {}) };
}

RGBuffer RGBuilder::previous(RGBuffer buffer)
{
    if(!buffer) { return {}; }
    const auto* native = get_previous_copy(*graph, graph->get_res(buffer));
    if(!native) { return {}; }
    return RGBuffer{ import_native(*native, {}) };
}

u32 RGBuilder::add_access(const RGAccess& a)
{
    const auto& res = graph->get_res(a.resource);
    if(!res.is_buffer() && a.stage.any())
    {
        // make sure this pass does not access the same resource twice
        for(const auto id : pass->accesses)
        {
            const auto& other = graph->get_acc(id);
            if(other.resource != a.resource || other.stage.empty() || other.layout == a.layout) { continue; }
            ENG_WARN("Pass \"{}\" accesses \"{}\" in different layouts; the later access wins.", pass->name.as_view(),
                     res.name.as_view());
        }
    }

    graph->accesses.push_back(a);
    const auto ret = (u32)graph->accesses.size() - 1;
    pass->accesses.push_back(ret);
    return ret;
}

RGImage RGBuilder::access_resource(RGImage image, ImageLayout layout, Flags<PipelineStage> stage,
                                   Flags<PipelineAccess> access, std::optional<ImageFormat> format,
                                   std::optional<ImageViewType> type, Range32u mips, Range32u layers)
{
    if(!image) { return {}; }
    const auto last = [](Range32u r) { return r.size == ~0u ? ~0u : r.offset + r.size - 1; };
    add_access(RGAccess{
        .resource = image.index,
        .image_view = ImageView::init(graph->get_img(image), format, type, mips.offset, last(mips), layers.offset, last(layers)),
        .layout = layout,
        .stage = stage,
        .access = access,
    });
    return image;
}

RGBuffer RGBuilder::access_resource(RGBuffer buffer, Flags<PipelineStage> stage, Flags<PipelineAccess> access, Range64u range)
{
    if(!buffer) { return {}; }
    add_access(RGAccess{
        .resource = buffer.index,
        .buffer_view = BufferView::init(graph->get_buf(buffer), range.offset, range.size),
        .stage = stage,
        .access = access,
    });
    return buffer;
}

const RGAccess& RGBuilder::get_pass_acc(u32 resource, u32 nth) const
{
    // the no-op access that only added the resource is used only if the pass has no other access to it
    const RGAccess* found{};
    u32 count = 0;
    for(const auto id : pass->accesses)
    {
        const auto& acc = graph->get_acc(id);
        if(acc.resource != resource) { continue; }
        if(acc.is_first && !acc.clear) { continue; }
        if(count++ == nth) { return acc; }
        found = &acc;
    }
    if(nth == ~0u && found) { return *found; }
    ENG_ASSERT(false, "Pass \"{}\" has no access {} to resource \"{}\".", pass->name.as_view(), nth,
               graph->get_res(resource).name.as_view());
    static const RGAccess null_access{ .buffer_view = {} };
    return null_access;
}

const RGAccess& RGBuilder::get_acc(RGImage image, u32 nth) const
{
    ENG_ASSERT(image);
    return get_pass_acc(image.index, nth);
}

const RGAccess& RGBuilder::get_acc(RGBuffer buffer, u32 nth) const
{
    ENG_ASSERT(buffer);
    return get_pass_acc(buffer.index, nth);
}

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
    gfx::set_debug_name(((CommandBufferVk*)pass->cmd)->m_cmd, pass->name.as_view());
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

    // calculate lifetimes
    for(auto i = 0u; i < passes.size(); ++i)
    {
        for(const auto a : passes[i].pass->accesses)
        {
            auto& res = get_res(get_acc(a).resource);
            if(res.first_pass == ~0u) { res.first_pass = i; }
            res.last_pass = i;
        }
    }

    pass_releases.clear();
    pass_releases.resize(passes.size());
    std::vector<std::vector<u32>> pass_acquires(passes.size());
    for(auto i = 0u; i < resources.size(); ++i)
    {
        const auto& res = resources[i];
        ENG_ASSERT(res.first_pass != ~0u);
        if(res.first_pass == ~0u) { continue; }
        pass_releases[res.last_pass].push_back(i);
        if(res.is_aliased()) { pass_acquires[res.first_pass].push_back(i); }
    }

    // allocate and release temporal memory
    for(auto i = 0u; i < passes.size(); ++i)
    {
        for(const auto id : pass_acquires[i])
        {
            auto& res = get_res(id);
            RendererMemoryRequirements reqs;
            void* base;
            size_t offset;
            if(res.is_buffer()) { get_renderer().backend->get_memory_requirements(res.as_buffer().get(), reqs); }
            else { get_renderer().backend->get_memory_requirements(res.as_image().get(), reqs); }
            res.aliased_memory = allocator->allocate(reqs);
            allocator->get_offset_and_base(res.aliased_memory, base, offset);
            if(res.is_buffer()) { get_renderer().backend->bind_aliasable_memory(res.as_buffer().get(), base, offset); }
            else { get_renderer().backend->bind_aliasable_memory(res.as_image().get(), base, offset); }
        }
        for(const auto id : pass_releases[i])
        {
            auto& res = get_res(id);
            if(!res.aliased_memory) { continue; }
            allocator->free(res.aliased_memory);
            res.aliased_memory = nullptr;
        }
    }

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

    bool batch_empty = true;
    // go over render passes
    for(u32 pi = 0; pi < passes.size(); ++pi)
    {
        RGPass* p = passes[pi].pass.get();

        ICommandBuffer* sync_cmd{};
        const auto get_sync_cmd = [this, &sync_cmd] {
            if(!sync_cmd) { sync_cmd = cmd_pools[0]->begin(); }
            return sync_cmd;
        };

        // go over pass' accesses and record barriers
        for(const auto accid : p->accesses)
        {
            const auto& acc = get_acc(accid);
            auto& res = get_res(acc.resource);

            if(acc.is_first) { get_or_init_resource_state(*this, res); }

            record_barrier_if_needed(get_sync_cmd, res, acc, acc.clear.has_value());

            if(acc.clear)
            {
                auto* cmd = get_sync_cmd();
                const auto& img = res.as_image().get();
                if(acc.clear->is_color())
                {
                    const auto c = acc.clear->get_color().color;
                    cmd->clear_color(img, Color4f{ c.x, c.y, c.z, c.w });
                }
                else
                {
                    const auto ds = acc.clear->get_ds();
                    cmd->clear_depth_stencil(img, ds.depth, ds.stencil);
                }
            }
        }
        if(sync_cmd) { cmd_pools[0]->end(sync_cmd); }

        // execute pass

        RGBuilder pb{ p, this };
        p->execute(pb);
        if(p->cmd)
        {
            p->cmd->end_label();
            p->cmd->write_timestamp(p->query->pool, PipelineStage::ALL, p->query->index + 1);
            cmd_pools[0]->end(p->cmd);
        }

        // submit all work in the batch before this pass, so it is not forced to wait on this pass' wait sync deps, if any.
        const bool pass_has_wait_sync_deps = p->cmd && std::any_of(p->cmd->sync_deps.begin(), p->cmd->sync_deps.end(),
                                                                   [](const auto& d) { return d.wait; });
        if(pass_has_wait_sync_deps && !batch_empty)
        {
            queue->submit();
            batch_empty = true;
        }

        if(sync_cmd)
        {
            queue->with_cmd_buf(sync_cmd);
            batch_empty = false;
        }
        if(p->cmd)
        {
            queue->with_cmd_buf(p->cmd);
            batch_empty = false;
        }

        // destroy transient resources, keep persistent ones
        for(const auto id : pass_releases[pi])
        {
            auto& res = get_res(id);
            if(res.is_persistent) { persistent_states_map[RGResource::make_key(res.native)] = res.state; }
            else { free_resource(res); }
        }
    }
    queue->signal_sync(sems[0], PipelineStage::ALL);
    queue->submit();

    resources.clear();
    accesses.clear();
    passes.clear();
    pass_releases.clear();
    namedpasses.clear();
    ++frame;

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
        dr.persistent = r.is_persistent;
        dr.aliased_memory = r.is_aliased();
    }
    for(u32 i = 0; i < rg->passes.size(); ++i)
    {
        const auto* p = rg->passes[i].pass.get();
        auto& dp = passes.emplace_back(Pass{ p->name.c_str(), {}, p->query });
        for(const auto& pa : p->accesses)
        {
            const auto& pacc = rg->get_acc(pa);
            const auto& pres = rg->get_res(pacc.resource);
            dp.accesses.push_back(Access{ pacc.resource, pacc.stage, pacc.access, pacc.layout, pres.last_pass == i });
        }
    }
}

} // namespace gfx
} // namespace eng
