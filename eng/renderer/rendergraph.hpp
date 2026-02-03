#pragma once

#include <vector>
#include <span>
#include <variant>
#include <unordered_set>
#include <unordered_map>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

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

    // struct IdGenerator
    //{
    //     template <typename ConcretePass> static uint32_t get()
    //     {
    //         static uint32_t id = gid.fetch_add(1);
    //         return id;
    //     }
    //     inline static std::atomic_uint32_t gid{};
    // };

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
    };

    struct ResourceAccess
    {
        bool is_read() const { return (access & PipelineAccess::READS) > 0; }
        bool is_write() const { return (access & PipelineAccess::WRITES) > 0; }
        bool is_import_only() const { return !stage && !access; }
        Handle<Resource> resource;
        Handle<ResourceAccess> prev_access;
        union {
            BufferView buffer; // if resource->is_buffer() == true or layout == undefined
            ImageView image;
        };
        ImageLayout layout{ ImageLayout::UNDEFINED };
        Flags<PipelineStage> stage;
        Flags<PipelineAccess> access;
    };

    struct ExecutionGroup
    {
        std::vector<Pass*> passes;
    };

    struct PassBuilder
    {
        Handle<ResourceAccess> import_resource(const Resource::NativeResource& resource, const std::optional<Clear>& clear = {})
        {
            const auto it = std::find_if(rg->resources.begin(), rg->resources.end(),
                                         [&resource](const auto& e) { return e.native == resource; });
            if(it != rg->resources.end()) { return it->last_access; }
            rg->resources.push_back(Resource{ resource, {} });
            const auto ret = add_access(ResourceAccess{ .resource = Handle<Resource>{ (uint32_t)rg->resources.size() - 1 },
                                                        .buffer = {},
                                                        .layout = ImageLayout::UNDEFINED,
                                                        .stage = {},
                                                        .access = {} });
            ENG_ASSERT(!clear);
            // if(clear) { p->clears.emplace_back(ret, *clear); }
            return ret;
        }

        Handle<ResourceAccess> create_resource(Buffer&& a, bool persistent = false)
        {
            ENG_ASSERT(a.name.size() > 0);
            if(persistent)
            {
                const auto hash = hash::combine_fnv1a(p->name, a.name);
                if(auto it = rg->persistent_resources.find(hash); it != rg->persistent_resources.end())
                {
                    if(it->second.pass_name != p->name)
                    {
                        ENG_ERROR("Hash collision");
                        return {};
                    }
                    return import_resource(it->second.resource.native);
                }
                else
                {
                    auto res = Engine::get().renderer->make_buffer(std::move(a));
                    auto persistent_it = rg->persistent_resources.emplace(
                        hash, PersistentStorage{ .pass_name = p->name, .resource = Resource{ .native = res, .is_persistent = true } });
                    return import_resource(persistent_it.first->second.resource.native);
                }
            }
            return import_resource(Engine::get().renderer->make_buffer(std::move(a)));
        }

        Handle<ResourceAccess> create_resource(Image&& a, bool persistent = false, const std::optional<Clear>& clear = {})
        {
            ENG_ASSERT(a.name.size() > 0);
            if(persistent)
            {
                const auto hash = hash::combine_fnv1a(p->name, a.name);
                if(auto it = rg->persistent_resources.find(hash); it != rg->persistent_resources.end())
                {
                    if(it->second.pass_name != p->name)
                    {
                        ENG_ERROR("Hash collision");
                        return {};
                    }
                    return import_resource(it->second.resource.native, clear);
                }
                else
                {
                    auto res = Engine::get().renderer->make_image(std::move(a));
                    auto persistent_it = rg->persistent_resources.emplace(
                        hash, PersistentStorage{ .pass_name = p->name, .resource = Resource{ .native = res, .is_persistent = true } });
                    return import_resource(persistent_it.first->second.resource.native, clear);
                }
            }
            return import_resource(Engine::get().renderer->make_image(std::move(a)), clear);
        }

        Handle<ResourceAccess> sample_texture(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                              std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                              Range32u layers = { 0u, ~0u })
        {
            const auto stage = p->is_graphics()  ? PipelineStage::FRAGMENT
                               : p->is_compute() ? PipelineStage::COMPUTE_BIT
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
            const auto stage = p->is_graphics()  ? PipelineStage::FRAGMENT
                               : p->is_compute() ? PipelineStage::COMPUTE_BIT
                                                 : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_READ_BIT;
            const auto layout = ImageLayout::GENERAL;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> write_image(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                           std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                           Range32u layers = { 0u, ~0u })
        {
            const auto stage = p->is_graphics()  ? PipelineStage::FRAGMENT
                               : p->is_compute() ? PipelineStage::COMPUTE_BIT
                                                 : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_WRITE_BIT;
            const auto layout = ImageLayout::GENERAL;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> read_write_image(Handle<ResourceAccess> acc, std::optional<ImageFormat> format = {},
                                                std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                                Range32u layers = { 0u, ~0u })
        {
            const auto stage = p->is_graphics()  ? PipelineStage::FRAGMENT
                               : p->is_compute() ? PipelineStage::COMPUTE_BIT
                                                 : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_RW;
            const auto layout = ImageLayout::GENERAL;
            return access_resource(acc, layout, stage, access, format, type, mips, layers);
        }

        Handle<ResourceAccess> read_buffer(Handle<ResourceAccess> acc, Range64u range = { 0ull, ~0ull })
        {
            const auto stage = p->is_graphics()  ? PipelineStage::VERTEX_BIT | PipelineStage::FRAGMENT
                               : p->is_compute() ? PipelineStage::COMPUTE_BIT
                                                 : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_READ_BIT;
            return access_resource(acc, stage, access, range);
        }

        Handle<ResourceAccess> write_buffer(Handle<ResourceAccess> acc, Range64u range = { 0ull, ~0ull })
        {
            const auto stage = p->is_compute() ? PipelineStage::COMPUTE_BIT : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_RW;
            return access_resource(acc, stage, access, range);
        }

        Handle<ResourceAccess> read_write_buffer(Handle<ResourceAccess> acc, Range64u range = { 0ull, ~0ull })
        {
            const auto stage = p->is_compute() ? PipelineStage::COMPUTE_BIT : PipelineStage::NONE;
            const auto access = PipelineAccess::STORAGE_RW;
            return access_resource(acc, stage, access, range);
        }

        Handle<ResourceAccess> access_resource(Handle<ResourceAccess> acc, ImageLayout layout, Flags<PipelineStage> stage,
                                               Flags<PipelineAccess> access, std::optional<ImageFormat> format = {},
                                               std::optional<ImageViewType> type = {}, Range32u mips = { 0u, ~0u },
                                               Range32u layers = { 0u, ~0u })
        {
            return add_access(ResourceAccess{
                .resource = rg->get_acc(acc).resource,
                .prev_access = acc,
                .image = ImageView::init(rg->get_img(acc), format, type, mips.offset, mips.size, layers.offset, layers.size),
                .layout = layout,
                .stage = stage,
                .access = access,
            });
        }

        Handle<ResourceAccess> access_resource(Handle<ResourceAccess> acc, Flags<PipelineStage> stage,
                                               Flags<PipelineAccess> access, Range64u range = { 0ull, ~0ull })
        {
            return add_access(ResourceAccess{
                .resource = rg->get_acc(acc).resource,
                .prev_access = acc,
                .buffer = BufferView{ rg->get_buf(acc), range },
                .stage = stage,
                .access = access,
            });
        }

        Handle<ResourceAccess> add_access(const ResourceAccess& a)
        {
            rg->accesses.push_back(a);
            const auto ret = Handle<ResourceAccess>{ (uint32_t)rg->accesses.size() - 1 };
            rg->get_res(ret).last_access = ret;
            p->accesses.push_back(ret);
            p->stage_mask |= a.stage;
            return ret;
        }

        ICommandBuffer* open_cmd_buf()
        {
            ENG_ASSERT(p->cmd == nullptr);
            p->cmd = rg->cmdpool->begin();
            p->cmd->begin_label(p->name);
            return p->cmd;
        }

        Pass* p{};
        RenderGraph* rg{};
    };

    // utility funcs for easy access to resources
    ResourceAccess& get_acc(Handle<ResourceAccess> a) { return accesses[*a]; }
    Resource& get_res(Handle<ResourceAccess> a) { return resources[*get_acc(a).resource]; }
    Handle<Buffer> get_buf(Handle<ResourceAccess> a) { return get_res(a).as_buffer(); }
    Handle<Image> get_img(Handle<ResourceAccess> a) { return get_res(a).as_image(); }

    void init(Renderer* r)
    {
        gq = r->get_queue(QueueType::GRAPHICS);
        cmdpool = gq->make_command_pool();
        sem = r->make_sync(SyncCreateInfo{ SyncType::TIMELINE_SEMAPHORE, 0, "rgraph sync sem" });
    }

    void add_graphics_pass(const std::string& name, const SetupFunc& setup_cb, const ExecFunc& exec_cb)
    {
        passes.push_back(Pass{ .name = name, .type = Pass::Type::GRAPHICS, .exec_cb = exec_cb });
        PassBuilder pb{ &passes.back(), this };
        return setup_cb(pb);
    }

    void add_compute_pass(const std::string& name, const SetupFunc& setup_cb, const ExecFunc& exec_cb)
    {
        passes.push_back(Pass{ .name = name, .type = Pass::Type::COMPUTE, .exec_cb = exec_cb });
        PassBuilder pb{ &passes.back(), this };
        return setup_cb(pb);
    }

    // template <typename T> T& get_pass(const std::string& name)
    //{
    //     const auto& p = passes[*namedpasses.at(name)];
    //     ENG_ASSERT(p.data != nullptr);
    //     return *static_cast<T*>(p.data);
    // }

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
                        // if reading, and layouts are not same, change layout and read in the next stage
                        if(pacc.layout != acc.layout) { return res.last_read_group + 1; }
                        // if layouts are compatible, we can read at the same time.
                        return res.last_read_group;
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
    }

    Sync* execute(Sync* wait_sync = nullptr)
    {
        cmdpool->reset();
        sem->reset();
        if(wait_sync != nullptr) { gq->wait_sync(wait_sync); }
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
                    {
                        if(acc.image.image->layout == acc.layout) { continue; }
                    }
                    if(layout_cmd == nullptr) { layout_cmd = cmdpool->begin(); }
                    const auto& pacc = get_acc(acc.prev_access);
                    Handle<Image> img = acc.image.image;
                    layout_cmd->barrier(img.get(), pacc.stage, pacc.access, acc.stage, acc.access, pacc.layout, acc.layout);
                }
            }

            if(layout_cmd)
            {
                cmdpool->end(layout_cmd);
                gq->with_cmd_buf(layout_cmd);
                gq->submit();
            }

            for(auto* p : g.passes)
            {
                PassBuilder pb{ p, this };
                p->exec_cb(*this, pb);
                if(p->cmd == nullptr) { continue; }
                p->cmd->end_label();
                cmdpool->end(p->cmd);
                gq->with_cmd_buf(p->cmd);
            }
            gq->wait_sync(sem, gstages);
            gq->signal_sync(sem, gstages);
            gq->submit();
        }

        resources.clear();
        accesses.clear();
        passes.clear();

        return sem;
    }

    SubmitQueue* gq{};
    CommandPoolVk* cmdpool{};
    Sync* sem{};

    struct PersistentStorage
    {
        std::string pass_name;
        Resource resource;
    };
    std::unordered_map<uint64_t, PersistentStorage> persistent_resources;
    std::vector<Resource> resources;
    std::vector<ResourceAccess> accesses;
    std::vector<Pass> passes;
    std::unordered_map<std::string, Handle<Pass>> namedpasses;
    std::vector<ExecutionGroup> groups;
};

} // namespace gfx
} // namespace eng