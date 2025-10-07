#pragma once

#include <vector>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/renderer.hpp>

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
        Resource(Handle<Image> a)
        {
            image = a;
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
            Resource(Handle<Buffer> buffer, uint32_t rg_res_idx, AccessType type, Flags<PipelineStage> stage,
                     Flags<PipelineAccess> access)
                : buffer(buffer), rg_res_idx(rg_res_idx), type(type), stage(stage), access(access), is_buffer(true)
            {
            }
            Resource(Handle<ImageView> imgview, uint32_t rg_res_idx, AccessType type, Flags<PipelineStage> stage,
                     Flags<PipelineAccess> access, ImageLayout layout, bool from_undefined = false)
                : imgview(imgview), rg_res_idx(rg_res_idx), type(type), stage(stage), access(access), is_buffer(false),
                  layout(layout), from_undefined(from_undefined)
            {
            }
            Resource(const Resource& r) noexcept { *this = r; }
            Resource& operator=(const Resource& r) noexcept
            {
                if(r.is_buffer) { buffer = r.buffer; }
                else { imgview = r.imgview; }
                rg_res_idx = r.rg_res_idx;
                type = r.type;
                stage = r.stage;
                access = r.access;
                layout = r.layout;
                is_buffer = r.is_buffer;
                from_undefined = r.from_undefined;
                return *this;
            }
            union {
                Handle<Buffer> buffer{};
                Handle<ImageView> imgview;
            };
            uint32_t rg_res_idx;
            AccessType type;
            Flags<PipelineStage> stage;
            Flags<PipelineAccess> access;
            ImageLayout layout{ ImageLayout::UNDEFINED };
            bool is_buffer;
            bool from_undefined{ false };
        };
        std::string name;
        uint32_t value;
        Callback<void(SubmitQueue*, CommandBuffer*)> render_cb;
        std::vector<Resource> resources;
    };

    struct PassResourceBuilder
    {
        Handle<Buffer> access(Handle<Buffer> r, AccessType type, Flags<PipelineStage> stage, Flags<PipelineAccess> access)
        {
            auto* res = graph->find_resource(r);
            if(!res) { res = &graph->resources.emplace_back(RenderGraph::Resource{ r }); }
            auto pr = Pass::Resource{ r, (uint32_t)(std::distance(graph->resources.data(), res)), type, stage, access };
            pass->resources.push_back(pr);
            return r;
        }
        Handle<ImageView> access(Handle<ImageView> r, AccessType type, Flags<PipelineStage> stage,
                                 Flags<PipelineAccess> access, ImageLayout layout, bool from_undefined = false)
        {
            auto img = r->image;
            auto* res = graph->find_resource(img);
            if(!res) { res = &graph->resources.emplace_back(RenderGraph::Resource{ r->image }); }
            auto pr = Pass::Resource{
                r, (uint32_t)(std::distance(graph->resources.data(), res)), type, stage, access, layout, from_undefined
            };
            pass->resources.push_back(pr);
            return r;
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
            ImageLayout last_read_layout{};
            uint32_t last_read_stage{ ~0u };
            uint32_t last_write_stage{ ~0u };
        };
        stages.clear();
        stages.resize(passes.size());
        std::unordered_map<uint32_t, ResourceHistory> rhist;
        uint32_t last_stage = 0u;

        // find last stage that used the resource and get the stage after this one
        const auto get_res_stage = [&rhist](Pass::Resource& res) {
            const auto& hist = rhist[res.rg_res_idx];
            const auto is_read = ((uint32_t)res.type & (uint32_t)AccessType::READ_BIT) == (uint32_t)AccessType::READ_BIT;
            const auto is_write = ((uint32_t)res.type & (uint32_t)AccessType::WRITE_BIT) == (uint32_t)AccessType::WRITE_BIT;
            if(is_write) { return std::max(hist.last_read_stage + 1, hist.last_write_stage + 1); }
            else if(is_read)
            {
                // this if can be extended to exclude conflicing reads. right now only mismatching layouts qualification
                if(!res.is_buffer && hist.last_read_layout != res.layout)
                {
                    return std::max(hist.last_read_stage + 1, hist.last_write_stage + 1);
                }
                return hist.last_write_stage + 1;
            }
            ENG_ERROR("Invalid access type.");
            return ~0u;
        };
        const auto update_hist = [&rhist, &last_stage](Pass& p, uint32_t stage) {
            for(auto& e : p.resources)
            {
                auto& hist = rhist[e.rg_res_idx];
                const auto is_read = ((uint32_t)e.type & (uint32_t)AccessType::READ_BIT) == (uint32_t)AccessType::READ_BIT;
                const auto is_write = ((uint32_t)e.type & (uint32_t)AccessType::WRITE_BIT) == (uint32_t)AccessType::WRITE_BIT;
                if(is_read)
                {
                    hist.last_read_stage = stage;
                    if(!e.is_buffer) { hist.last_read_layout = e.layout; }
                }
                if(is_write) { hist.last_write_stage = stage; }
            }
            last_stage = std::max(last_stage, stage);
        };
        for(auto& p : passes)
        {
            // sort so images are at the end
            std::sort(p.resources.begin(), p.resources.end(), [](const auto& a, const auto& b) {
                return (a.is_buffer && b.is_buffer) ? a.rg_res_idx < b.rg_res_idx : a.is_buffer ? true : false;
            });
            const auto pstage = [&p, &get_res_stage] {
                auto stage = 0u;
                for(auto& e : p.resources)
                {
                    stage = std::max(stage, get_res_stage(e));
                }
                return stage;
            }();
            update_hist(p, pstage);
            stages.at(pstage).passes.push_back(&p);
        }
        stages.resize(last_stage + 1);
    }

    void render()
    {
        struct BarrierData
        {
            const Pass::Resource* res{};
        };
        std::unordered_map<uint32_t, BarrierData> rhists; // todo: this can be precomputed

        gcmdpool->reset();
        for(auto si = 0u; si < stages.size(); ++si)
        {
            auto& s = stages.at(si);
            for(auto& p : s.passes)
            {
                auto* cmd = gcmdpool->begin();
                // todo: move this inside command buffer's begin
                VkDebugUtilsLabelEXT vkdlab{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                             .pLabelName = p->name.c_str(),
                                             .color = { 0.0f, 0.0f, 1.0f, 1.0f } };
                vkCmdBeginDebugUtilsLabelEXT(cmd->cmd, &vkdlab);
                // combine barrier flags for one big barrier for buffers
                Flags<PipelineStage> bsrcs;
                Flags<PipelineAccess> bsrca;
                Flags<PipelineStage> bdsts;
                Flags<PipelineAccess> bdsta;
                bool bbarrier = false;
                for(auto& r : p->resources)
                {
                    auto& rhist = rhists[r.rg_res_idx];
                    Flags<PipelineStage> srcs = PipelineStage::ALL;
                    Flags<PipelineAccess> srca = PipelineAccess::NONE;
                    ImageLayout srcl = ImageLayout::UNDEFINED;
                    if(rhist.res)
                    {
                        srcs = rhist.res->stage;
                        srca = rhist.res->access;
                        if(!r.is_buffer) { srcl = rhist.res->layout; }
                    }
                    if(r.is_buffer)
                    {
                        bsrcs |= srcs;
                        bdsts |= r.stage;
                        bsrca |= srca;
                        bdsta |= r.access;
                        bbarrier = true;
                    }
                    else { cmd->barrier(r.imgview->image.get(), srcs, srca, r.stage, r.access, srcl, r.layout); }
                    rhist.res = &r;
                }
                if(bbarrier) { cmd->barrier(bsrcs, bsrca, bdsts, bdsta); }
                p->render_cb(gq, cmd);
                vkCmdEndDebugUtilsLabelEXT(cmd->cmd);
                gcmdpool->end(cmd);
                gq->with_cmd_buf(cmd);
                gq->submit();
            }
        }
        passes.clear();
    }

  private:
    struct Stage
    {
        std::vector<Pass*> passes;
    };

    Resource* find_resource(Handle<Buffer> a)
    {
        for(auto& e : resources)
        {
            if(e.is_buffer && e.buffer == a) { return &e; }
        }
        return nullptr;
    }
    Resource* find_resource(Handle<Image> a)
    {
        for(auto& e : resources)
        {
            if(!e.is_buffer && e.image == a) { return &e; }
        }
        return nullptr;
    }

    SubmitQueue* gq{};
    CommandPool* gcmdpool{};
    std::vector<Resource> resources;
    std::vector<Pass> passes;
    std::vector<Stage> stages;
};

} // namespace gfx
} // namespace eng