#pragma once

#include <vector>
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
    enum class AccessType
    {
        NONE = 0x0,
        READ_BIT = 0x1,
        WRITE_BIT = 0x2,
        RW = READ_BIT | WRITE_BIT,
    };

    struct Resource
    {
        Resource(Handle<Buffer> a)
        {
            buffer = a;
            is_buffer = true;
        }
        Resource(Handle<ImageView> a)
        {
            image = a->image;
            is_buffer = false;
        }
        Resource(const Resource& r) noexcept { *this = r; }
        Resource& operator=(const Resource& r) noexcept
        {
            if(is_buffer) { buffer = r.buffer; }
            else { image = r.image; }
            is_buffer = r.is_buffer;
            return *this;
        }
        bool operator==(Handle<Buffer> a) const { return is_buffer && buffer == a; }
        bool operator==(Handle<ImageView> a) const { return !is_buffer && image == a->image; }
        union {
            Handle<Buffer> buffer{};
            Handle<Image> image;
        };
        bool is_buffer;
    };

    struct Pass
    {
        struct Resource
        {
            Resource(uint32_t res, AccessType type, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                     ImageLayout layout = ImageLayout::UNDEFINED, bool from_undefined = false)
                : rgres(res), type(type), stage(stage), access(access), layout(layout), from_undefined(from_undefined)
            {
            }
            bool is_read() const
            {
                return ((uint32_t)type & (uint32_t)AccessType::READ_BIT) == (uint32_t)AccessType::READ_BIT;
            }
            bool is_write() const
            {
                return ((uint32_t)type & (uint32_t)AccessType::WRITE_BIT) == (uint32_t)AccessType::WRITE_BIT;
            }
            uint32_t rgres;
            AccessType type;
            Flags<PipelineStage> stage;
            Flags<PipelineAccess> access;
            ImageLayout layout{ ImageLayout::UNDEFINED };
            bool from_undefined{ false };
        };

        std::string name;
        uint32_t value;
        Callback<void(SubmitQueue*, CommandBuffer*)> render_cb;
        std::vector<Resource> resources;
    };

    struct PassResourceBuilder
    {
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

        template <typename T>
        void access(Handle<T> h, Flags<PipelineStage> stage, Flags<PipelineAccess> access,
                    ImageLayout layout = ImageLayout::UNDEFINED, bool from_undefined = false)
        {
            const auto acctype = get_access_from_flags(access);
            auto res = graph->get_resource(h);
            auto pr = Pass::Resource{ res, acctype, stage, access, layout, from_undefined };
            pass->resources.push_back(pr);
        }
        RenderGraph* graph;
        Pass* pass;
    };

    struct PassCreateInfo
    {
        std::string name;
        uint32_t value;
    };

    void init(Renderer* r)
    {
        gq = r->get_queue(QueueType::GRAPHICS);
        gcmdpool = gq->make_command_pool();
        sync_sem = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
    }

    auto add_pass(const PassCreateInfo& info, const auto& builder_cb, const auto& render_cb)
    {
        auto it = std::upper_bound(passes.begin(), passes.end(), info.value,
                                   [](uint32_t v, const Pass& p) { return v < p.value; });
        it = passes.emplace(it, info.name, info.value, render_cb);
        PassResourceBuilder builder{ this, &*it };
        return builder_cb(builder);
    }

    void compile()
    {
        struct ResourceHistory
        {
            ImageLayout last_read_layout{ ImageLayout::UNDEFINED };
            ImageLayout last_layout{ ImageLayout::UNDEFINED };
            uint32_t last_read_stage{ ~0u };
            uint32_t last_write_stage{ ~0u };
        };
        stages.clear();
        stages.resize(passes.size());
        std::unordered_map<uint32_t, ResourceHistory> rgreshists;
        uint32_t last_stage = 0u;

        // find last stage that used the resource and get the stage after this one
        const auto get_stage_for_res = [&rgreshists, this](const Pass::Resource& res) {
            const auto& hist = rgreshists[res.rgres];
            if(res.is_write()) { return std::max(hist.last_read_stage + 1, hist.last_write_stage + 1); }
            else if(res.is_read())
            {
                // this if can be extended to exclude conflicing reads. right now only mismatching layouts are checked
                if(!resources.at(res.rgres).is_buffer && hist.last_read_layout != res.layout)
                {
                    return std::max(hist.last_read_stage + 1, hist.last_write_stage + 1);
                }
                return hist.last_write_stage + 1;
            }
            ENG_ERROR("Invalid access type.");
            return ~0u;
        };
        const auto update_hist = [&rgreshists, &last_stage, this](Pass& pass, uint32_t stageidx) {
            auto& stage = stages.at(stageidx);
            for(auto& pr : pass.resources)
            {
                auto& rhist = rgreshists[pr.rgres];
                if(pr.is_read() || pr.is_write())
                {
                    if(!resources.at(pr.rgres).is_buffer)
                    {
                        if(pr.layout != rhist.last_layout)
                        {
                            stage.imgbarriers.push_back(Stage::ImageBarrier{
                                resources.at(pr.rgres).image, pr.stage, pr.access,
                                pr.from_undefined ? ImageLayout::UNDEFINED : rhist.last_read_layout, pr.layout });
                        }
                        rhist.last_layout = pr.layout;
                    }
                    stage.all_stages |= pr.stage;
                }
                if(pr.is_read())
                {
                    rhist.last_read_stage = stageidx;
                    if(!resources.at(pr.rgres).is_buffer) { rhist.last_read_layout = pr.layout; }
                }
                if(pr.is_write()) { rhist.last_write_stage = stageidx; }
            }
            last_stage = std::max(last_stage, stageidx);
        };
        for(auto& pass : passes)
        {
            // sort so images are at the end
            std::sort(pass.resources.begin(), pass.resources.end(), [this](const auto& a, const auto& b) {
                return (resources.at(a.rgres).is_buffer && resources.at(b.rgres).is_buffer) ? a.rgres < b.rgres
                       : resources.at(a.rgres).is_buffer                                    ? true
                                                                                            : false;
            });
            const auto stage = [&pass, &get_stage_for_res] {
                auto stage = 0u;
                for(auto& e : pass.resources)
                {
                    stage = std::max(stage, get_stage_for_res(e));
                }
                return stage;
            }();
            update_hist(pass, stage);
            stages.at(stage).passes.push_back(&pass);
        }
        stages.resize(last_stage + 1);
    }

    Sync* execute(Sync* wait_sync = nullptr)
    {
        std::string passnames;
        uint64_t wait_sync_val{ 0 };
        gcmdpool->reset();
        sync_sem->reset(wait_sync_val);
        if(wait_sync) { gq->wait_sync(wait_sync, PipelineStage::ALL, wait_sync->wait_gpu()); }
        ENG_LOG("[RGRAPH] Start");
        for(auto si = 0u; si < stages.size(); ++si)
        {
            auto& s = stages.at(si);
            passnames.clear();
            for(const auto& p : s.passes)
            {
                passnames += ENG_FMT("\"{}\" ", p->name);
            }
            ENG_LOG("[RGRAPH] Stage {} with passes: [{}]", si, passnames);

            for(auto i = 0u; i < s.passes.size(); ++i)
            {
                const auto& p = s.passes.at(i);
                auto* cmd = gcmdpool->begin();
                if(i == 0)
                {
                    for(const auto& ib : s.imgbarriers)
                    {
                        cmd->barrier(ib.image.get(), PipelineStage::NONE, PipelineAccess::NONE, ib.stage, ib.access,
                                     ib.from, ib.to);
                    }
                }
                // todo: move this inside command buffer's begin
                VkDebugUtilsLabelEXT vkdlab{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                             .pLabelName = p->name.c_str(),
                                             .color = { 0.0f, 0.0f, 1.0f, 1.0f } };
                vkCmdBeginDebugUtilsLabelEXT(cmd->cmd, &vkdlab);
                // combine barrier flags for one big barrier for buffers
                p->render_cb(gq, cmd);
                vkCmdEndDebugUtilsLabelEXT(cmd->cmd);
                gcmdpool->end(cmd);
                gq->with_cmd_buf(cmd);
            }
            gq->wait_sync(sync_sem, s.all_stages, wait_sync_val);
            wait_sync_val = sync_sem->signal_gpu();
            gq->signal_sync(sync_sem, s.all_stages, wait_sync_val);
            gq->submit();
        }
        ENG_LOG("[RGRAPH] End");
        passes.clear();
        return sync_sem;
    }

  private:
    struct Stage
    {
        struct ImageBarrier
        {
            Handle<Image> image;
            Flags<PipelineStage> stage;
            Flags<PipelineAccess> access;
            ImageLayout from;
            ImageLayout to;
        };

        std::vector<Pass*> passes;
        std::vector<ImageBarrier> imgbarriers;
        Flags<PipelineStage> all_stages{};
    };

    template <typename T> uint32_t get_resource(Handle<T> a)
    {
        for(auto i = 0u; i < resources.size(); ++i)
        {
            if(resources.at(i) == a) { return i; }
        }
        resources.emplace_back(a);
        return resources.size() - 1;
    }

    SubmitQueue* gq{};
    CommandPool* gcmdpool{};
    Sync* sync_sem{};
    std::vector<Resource> resources;
    std::vector<Pass> passes;
    std::vector<Stage> stages;
};

} // namespace gfx
} // namespace eng