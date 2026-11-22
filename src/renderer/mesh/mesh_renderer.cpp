#include <eng/renderer/mesh/mesh_renderer.hpp>
#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/math/align.hpp>

namespace eng
{
namespace gfx
{

static void ensure_buffer(Handle<Buffer>& buf, std::string_view name, size_t size, Flags<BufferUsage> usage)
{
    if(!buf) { buf = get_renderer().make_buffer(name, Buffer::init(size, usage)); }
    else { get_renderer().resize_buffer(buf, size, false); }
}

void MeshRenderer::build_passes()
{
    const auto& qgroup = Engine::get().ecs->get_query_group<ecsc::Mesh>();
    if(m_meshes_hash == qgroup.hash) { return; }
    m_meshes_hash = qgroup.hash;

    for(auto i = 0u; i < (u32)MeshPassType::LAST_ENUM; ++i)
    {
        const auto type = (MeshPassType)i;
        auto& pass = m_pass_datas_arr[i];
        auto instances = gather_instances(type, qgroup.entities);
        // group by pipeline (one batch each), then by meshlet (one indirect command each)
        std::ranges::sort(instances, {}, [](const MeshletInstance& a) { return std::tie(a.pipeline, a.meshlet); });
        upload(type, pass, build_pass(pass, instances));
    }
}

MeshRenderer::SetupPassData MeshRenderer::setup(MeshPassType type, RGBuilder& b) const
{
    const auto& pass = m_pass_datas_arr[(int)type];
    SetupPassData data{};
    if(pass.batches_vec.empty()) { return data; }
    data.constants = b.read_buffer(get_renderer().targets.constants);
    data.index = b.read_index(b.import_resource(get_renderer().bufs.indices));
    data.indirect = b.read_indirect(b.import_resource(pass.indirect_buf));
    data.gpuinstances = b.read_buffer(b.import_resource(pass.instance_buf));
    return data;
}

void MeshRenderer::draw(MeshPassType type, ICommandBuffer& cmd)
{
    const auto& pass = m_pass_datas_arr[(int)type];
    const auto cmd_size = get_renderer().backend->get_indirect_indexed_command_size();
    cmd.bind_index(get_renderer().bufs.indices.get(), 0ull, VK_INDEX_TYPE_UINT16);
    u32 batch_idx = 0;
    for(const auto& batch : pass.batches_vec)
    {
        cmd.bind_pipeline(batch.pipeline.get());
        cmd.draw_indexed_indirect_count(pass.indirect_buf.get(), pass.indirect_cmds_offset + batch.first_command * cmd_size,
                                        pass.indirect_buf.get(), batch_idx * sizeof(u32), batch.command_count, cmd_size);
        ++batch_idx;
    }
}

MeshRenderer::InstancesVec MeshRenderer::gather_instances(MeshPassType type, std::span<const ecs::EntityId> entities)
{
    InstancesVec instances;
    for(auto e : entities)
    {
        const auto& msh = Engine::get().ecs->get<ecsc::Mesh>(e);
        ENG_ASSERT(msh.gpu_index != ~0u);
        for(auto rmsh : msh.render_meshes)
        {
            const auto& rmat = rmsh->material.get();
            const auto& effect = rmat.mesh_pass->effects[(int)type];
            if(!effect) { continue; }
            if(type == MeshPassType::Z_PREPASS && rmat.mode != Material::Mode::OPAQUE) { continue; }

            const auto meshlets = rmsh->geometry->meshlet_range;
            for(auto i = 0u; i < meshlets.size; ++i)
            {
                instances.push_back(MeshletInstance{ .material = rmsh->material,
                                                     .pipeline = effect->pipeline,
                                                     .gpu_resource = msh.gpu_index,
                                                     .meshlet = meshlets.offset + i });
            }
        }
    }
    return instances;
}

MeshRenderer::BuildPassResult MeshRenderer::build_pass(PassData& pass, const InstancesVec& vec)
{
    const auto cmd_size = get_renderer().backend->get_indirect_indexed_command_size();
    BuildPassResult res;
    res.gpuinstanceids_vec.reserve(vec.size());
    pass.batches_vec.clear();

    u32 cmd_count = 0;
    for(auto first = 0u; first < vec.size();)
    {
        const auto& fi = vec[first];
        auto last = first + 1;
        while(last < vec.size() && vec[last].pipeline == fi.pipeline && vec[last].meshlet == fi.meshlet)
        {
            ++last;
        }

        if(pass.batches_vec.empty() || pass.batches_vec.back().pipeline != fi.pipeline)
        {
            pass.batches_vec.push_back({ .pipeline = fi.pipeline, .first_command = cmd_count, .command_count = 0 });
            res.counts_vec.push_back(0);
        }
        ++pass.batches_vec.back().command_count;
        ++res.counts_vec.back();

        const auto& meshlet = get_renderer().meshlets[fi.meshlet];
        res.cmds_vec.resize(res.cmds_vec.size() + cmd_size);
        get_renderer().backend->make_indirect_indexed_command(res.cmds_vec.data() + cmd_count * cmd_size, meshlet.index_count,
                                                              last - first, meshlet.index_offset, meshlet.vertex_offset, first);
        for(auto i = first; i < last; ++i)
        {
            res.gpuinstanceids_vec.push_back(GPUInstanceId{
                .cmdi = cmd_count, .resi = first, .insti = vec[i].gpu_resource, .mati = vec[i].material.index() });
        }

        ++cmd_count;
        first = last;
    }
    return res;
}

void MeshRenderer::upload(MeshPassType type, PassData& pass, const BuildPassResult& res)
{
    pass.indirect_cmds_offset = align_up2(res.counts_vec.size() * sizeof(u32), 16);
    ensure_buffer(pass.indirect_buf, ENG_FMT("{} indirect buffer", to_string(type)),
                  pass.indirect_cmds_offset + res.cmds_vec.size(), BufferUsage::INDIRECT_BIT);
    ensure_buffer(pass.instance_buf, ENG_FMT("{} instance buffer", to_string(type)),
                  res.gpuinstanceids_vec.size() * sizeof(GPUInstanceId), BufferUsage::STORAGE_BIT);

    get_renderer().staging->copy(pass.indirect_buf.get(), res.counts_vec, 0ull);
    get_renderer().staging->copy(pass.indirect_buf.get(), res.cmds_vec, pass.indirect_cmds_offset);
    get_renderer().staging->copy(pass.instance_buf.get(), res.gpuinstanceids_vec, 0);
}

} // namespace gfx
} // namespace eng