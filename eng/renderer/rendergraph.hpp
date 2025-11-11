#pragma once

#include <vector>
#include <span>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

namespace eng
{
namespace gfx
{
class RenderGraph
{
  public:
    struct Resource
    {
        Resource(Handle<Buffer> a) : buffer(a), is_buffer(true) {}
        Resource(Handle<Image> a) : image(a), is_buffer(false) {}
        Resource(const Resource& a) noexcept
        {
            is_buffer = a.is_buffer;
            if(is_buffer) { buffer = a.buffer; }
            else { image = a.image; }
        }
        union {
            Handle<Buffer> buffer{};
            Handle<Image> image;
        };
        bool is_buffer{};
    };

    struct ResourceView
    {
        Handle<Resource> get(int32_t offset = 0) const
        {
            offset = std::clamp(offset, -(int32_t)count, (int32_t)count);
            return Handle<Resource>{ start + ((head + count + offset) % count) };
        }
        Handle<Resource> at(uint32_t idx) const { return Handle<Resource>{ start + idx }; }
        // sets the head before consumptions, ignoring any next() calls.
        // useful for swapchain image views.
        void set(uint32_t idx) { head = idx; }
        void next() { head = (head + 1) % count; }
        uint32_t head{};
        uint32_t start{};
        uint32_t count{};
    };

    class Pass
    {
      public:
        struct ResourceAccess
        {
            enum class AccessType
            {
                NONE = 0x0,
                READ_BIT = 0x1,
                WRITE_BIT = 0x2,
                RW = READ_BIT | WRITE_BIT,
            };
            static AccessType get_access_from_flags(Flags<PipelineAccess> f)
            {
                const Flags<PipelineAccess> reads = PipelineAccess::SHADER_READ_BIT | PipelineAccess::COLOR_READ_BIT |
                                                    PipelineAccess::DS_READ_BIT | PipelineAccess::STORAGE_READ_BIT |
                                                    PipelineAccess::INDIRECT_READ_BIT | PipelineAccess::TRANSFER_READ_BIT;
                const Flags<PipelineAccess> writes = PipelineAccess::SHADER_WRITE_BIT | PipelineAccess::COLOR_WRITE_BIT |
                                                     PipelineAccess::DS_WRITE_BIT | PipelineAccess::STORAGE_WRITE_BIT |
                                                     PipelineAccess::TRANSFER_WRITE_BIT;
                const bool has_reads = !!(f & reads);
                const bool has_writes = !!(f & writes);
                f &= ~(reads | writes);
                if(f) { assert(false && "Unhandled PipelineAccess enum values."); }
                return (has_reads && has_writes) ? AccessType::RW
                       : has_reads               ? AccessType::READ_BIT
                       : has_writes              ? AccessType::WRITE_BIT
                                                 : AccessType::NONE;
            }
            ResourceAccess(ResourceView* view, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                           ImageLayout layout = ImageLayout::UNDEFINED, bool from_undefined = false)
                : view(view), stage(stage), access(access), layout(layout), from_undefined(from_undefined),
                  type(get_access_from_flags(access))
            {
            }
            bool is_read() const { return (uint32_t)type & (uint32_t)AccessType::READ_BIT; }
            bool is_write() const { return (uint32_t)type & (uint32_t)AccessType::WRITE_BIT; }
            ResourceView* view;
            Flags<PipelineStage> stage;
            Flags<PipelineAccess> access;
            ImageLayout layout;
            bool from_undefined;
            AccessType type;
        };
        Pass(const std::string& name, uint32_t order) : name(name), order(order) {}
        virtual ~Pass() noexcept = default;
        virtual void setup() = 0;
        virtual void execute(RenderGraph* rg, SubmitQueue* q, CommandBuffer* cmd) = 0;
        void access(ResourceView& view, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                    ImageLayout layout = ImageLayout::UNDEFINED, bool from_undefined = false)
        {
            accesses.emplace_back(&view, stage, access, layout, from_undefined);
        }
        std::string name;
        uint32_t order;
        std::vector<ResourceAccess> accesses;
    };

    struct ExecutionGroup
    {
        struct ImageBarrier
        {
            Handle<Image> image;
            Flags<PipelineStage> stage;
            Flags<PipelineAccess> access;
            ImageLayout from;
            ImageLayout to;
        };
        Flags<PipelineStage> stages;
        std::vector<ImageBarrier> layout_transitions;
        std::vector<Pass*> passes;
    };

    void init(Renderer* r)
    {
        gq = r->get_queue(QueueType::GRAPHICS);
        gcmdpool = gq->make_command_pool();
        sync_sem = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
    }

    void add_pass(Pass* p)
    {
        auto it = std::upper_bound(passes.begin(), passes.end(), p->order,
                                   [](uint32_t val, const Pass* it) { return val < it->order; });
        passes.insert(it, p);
        namedpasses[p->name] = p;
    }

    template <typename T> const T& get_pass(const std::string& name) { return *static_cast<T*>(namedpasses.at(name)); }

    template <typename T> ResourceView import_resource(std::span<Handle<T>> a)
    {
        for(const auto& e : a)
        {
            resources.emplace_back(e);
        }
        return ResourceView{ 0, (uint32_t)resources.size() - (uint32_t)a.size(), (uint32_t)a.size() };
    }

    ResourceView make_resource(BufferDescriptor a, uint32_t copies)
    {
        const auto name = a.name + " {}";
        for(auto i = 0u; i < copies; ++i)
        {
            a.name = ENG_FMT_STR(name, i);
            resources.emplace_back(Engine::get().renderer->make_buffer(a));
        }
        return ResourceView{ 0, (uint32_t)resources.size() - copies, copies };
    }

    ResourceView make_resource(ImageDescriptor a, uint32_t copies)
    {
        const auto name = a.name + " {}";
        for(auto i = 0u; i < copies; ++i)
        {
            a.name = ENG_FMT_STR(name, i);
            resources.emplace_back(Engine::get().renderer->make_image(a));
        }
        return ResourceView{ 0, (uint32_t)resources.size() - copies, copies };
    }

    Resource& get_resource(const ResourceView& a) { return resources.at(*a.get()); }
    Resource& get_resource(const Handle<Resource>& a) { return resources.at(*a); }

    // ResourceView make_view(Handle<Resource> a, Handle<Resource> b)
    //{
    //     if(!a || !b)
    //     {
    //         assert(false);
    //         return {};
    //     }
    //     if(b < a) { std::swap(a, b); }
    //     return ResourceView{ 0, *a, (*b - *a) + 1 };
    // }

    void compile()
    {
        groups.clear();
        groups.resize(passes.size());

        struct ResourceAccessHistory
        {
            ImageLayout last_layout{ ImageLayout::UNDEFINED };
            uint32_t last_read_stage{ ~0u };
            uint32_t last_write_stage{ ~0u };
        };
        std::unordered_map<Handle<Resource>, ResourceAccessHistory> reshists;

        const auto calc_res_stage = [&reshists](const Pass::ResourceAccess& a) {
            const auto& hist = reshists[a.view->get()];
            if(a.is_write()) { return std::max(hist.last_read_stage + 1, hist.last_write_stage + 1); }
            if(a.is_read())
            {
                if(hist.last_layout != a.layout)
                {
                    return std::max(hist.last_read_stage + 1, hist.last_write_stage + 1);
                }
                return hist.last_write_stage + 1;
            }
            assert(false);
            return ~0u;
        };

        const auto update_history = [this, &reshists](const Pass::ResourceAccess& a, uint32_t stage) {
            auto& hist = reshists[a.view->get()];
            auto& group = groups[stage];
            if(a.is_read()) { hist.last_read_stage = stage; }
            if(a.is_write()) { hist.last_write_stage = stage; }
            if(a.layout != ImageLayout::UNDEFINED && hist.last_layout != a.layout)
            {
                group.layout_transitions.push_back(ExecutionGroup::ImageBarrier{
                    get_resource(*a.view).image, a.stage, a.access,
                    a.from_undefined ? ImageLayout::UNDEFINED : hist.last_layout, a.layout });
            }
            hist.last_layout = a.layout;
            group.stages |= a.stage;
            if(group.stages & PipelineStage::EARLY_Z_BIT) { group.stages |= PipelineStage::LATE_Z_BIT; }
        };

        auto last_stage = 0u;
        for(auto& p : passes)
        {
            p->setup();
            const auto stage = [&p, &calc_res_stage] {
                auto stage = 0u;
                for(const auto& a : p->accesses)
                {
                    const auto resstage = calc_res_stage(a);
                    stage = std::max(stage, resstage);
                }
                return stage;
            }();
            for(const auto& a : p->accesses)
            {
                update_history(a, stage);
            }
            groups[stage].passes.push_back(p);
            last_stage = std::max(last_stage, stage);
        }
        groups.resize(last_stage + 1);
    }

    Sync* execute(Sync* wait_sync = nullptr)
    {
        uint64_t wait_sync_val{ 0 };
        gcmdpool->reset();
        sync_sem->reset(wait_sync_val);
        if(wait_sync) { gq->wait_sync(wait_sync, PipelineStage::ALL, wait_sync->wait_gpu()); }
        std::string passnames;
        ENG_LOG("[RGRAPH] Start");
        for(auto si = 0u; si < groups.size(); ++si)
        {
            auto& g = groups.at(si);
            passnames.clear();
            for(const auto& p : g.passes)
            {
                passnames += ENG_FMT("\"{}\" ", p->name);
            }
            ENG_LOG("[RGRAPH] Stage {} with passes: [{}]", si, passnames);

            for(auto i = 0u; i < g.passes.size(); ++i)
            {
                const auto& p = g.passes.at(i);
                auto* cmd = gcmdpool->begin();
                if(i == 0)
                {
                    for(auto& e : g.layout_transitions)
                    {
                        cmd->barrier(e.image.get(), PipelineStage::ALL, PipelineAccess::NONE, e.stage, e.access, e.from, e.to);
                    }
                }
                // todo: move this inside command buffer's begin
                VkDebugUtilsLabelEXT vkdlab{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                             .pLabelName = p->name.c_str(),
                                             .color = { 0.0f, 0.0f, 1.0f, 1.0f } };
                vkCmdBeginDebugUtilsLabelEXT(cmd->cmd, &vkdlab);
                // combine barrier flags for one big barrier for buffers
                p->execute(this, gq, cmd);
                vkCmdEndDebugUtilsLabelEXT(cmd->cmd);
                gcmdpool->end(cmd);
                gq->with_cmd_buf(cmd);
            }
            gq->wait_sync(sync_sem, g.stages, wait_sync_val);
            wait_sync_val = sync_sem->signal_gpu();
            gq->signal_sync(sync_sem, g.stages, wait_sync_val);
            gq->submit();
        }
        ENG_LOG("[RGRAPH] End");
        for(auto& e : passes)
        {
            for(auto& a : e->accesses)
            {
                a.view->next();
            }
            e->accesses.clear();
        }
        passes.clear();
        namedpasses.clear();
        return sync_sem;
    }

    SubmitQueue* gq{};
    CommandPool* gcmdpool{};
    Sync* sync_sem{};
    std::vector<Resource> resources;
    std::vector<Pass*> passes;
    std::unordered_map<std::string, Pass*> namedpasses;
    std::vector<ExecutionGroup> groups;
};
} // namespace gfx
} // namespace eng