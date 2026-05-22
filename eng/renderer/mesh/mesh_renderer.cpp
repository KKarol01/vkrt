#include "mesh_renderer.hpp"
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

void MeshRenderer::instance_entity(ecs::EntityId entity)
{
    const auto& msh = get_engine().ecs->get<ecsc::Mesh>(entity);
    ENG_ASSERT(msh.gpu_resource != ~0u);
    for(auto rmsh : msh.render_meshes)
    {
        const auto& rmat = rmsh->material.get();
        const auto& rmp = rmat.mesh_pass.get();
        for(auto i = 0u; i < rmp.effects.size(); ++i)
        {
            if(rmp.effects[i])
            {
                if((MeshPassType)i == MeshPassType::Z_PREPASS)
                {
                    if(rmat.alpha_cutoff > 0.0) { continue; }
                }

                m_pass_datas_arr[i].meshes_vec.emplace_back(rmsh, msh.gpu_resource);
            }
        }
    }
}

void MeshRenderer::build_passes()
{
    for(auto i = 0u; i < (int)MeshPassType::LAST_ENUM; ++i)
    {
        auto& pass = m_pass_datas_arr[i];
        const auto hash = [&pass] {
            uint64_t hash{};
            for(const auto& h : pass.meshes_vec)
            {
                hash = ENG_HASH(hash, h.mesh);
            }
            return hash;
        }();

        if(hash == pass.meshes_hash)
        {
            pass.meshes_vec.clear();
            continue;
        }
        pass.meshes_hash = hash;
        auto instances = extract_mesh_instances((MeshPassType)i, pass);
        sort_mesh_instances(instances);
        auto ret = build_pass_from_instances(pass, instances);

        std::vector<uint32_t> counts;
        counts.reserve(pass.batches_vec.size());
        for(const auto& b : pass.batches_vec)
        {
            counts.push_back(b.command_count);
        }

        const auto cmds_byte_start = align_up2((counts.size() * sizeof(counts[0])), 16);
        const auto indirect_cap = cmds_byte_start + ret.cmds_vec.size();

        pass.indirect_cmds_offset = cmds_byte_start;

        struct MakeOrResizeBufData
        {
            Handle<Buffer>* buf;
            size_t size;
            StackString<64> name;
            Flags<BufferUsage> usage;
        };
        MakeOrResizeBufData mkorrszbufs[]{
            { &pass.indirect_buf, indirect_cap, ENG_FMT("{} indirect buffer", to_string((MeshPassType)i)), BufferUsage::INDIRECT_BIT },
            { &pass.instance_buf, ret.gpuinstanceids_vec.size() * sizeof(ret.gpuinstanceids_vec[0]),
              ENG_FMT("{} instance buffer", to_string((MeshPassType)i)), BufferUsage::STORAGE_BIT },
        };
        for(auto i = 0u; i < std::size(mkorrszbufs); ++i)
        {
            auto& d = mkorrszbufs[i];
            if(!*d.buf) { *d.buf = get_renderer().make_buffer(d.name.as_view(), Buffer::init(d.size, d.usage)); }
            else { get_renderer().resize_buffer(*d.buf, d.size, false); }
        }

        get_renderer().staging->copy(pass.indirect_buf.get(), counts, 0ull);
        get_renderer().staging->copy(pass.indirect_buf.get(), ret.cmds_vec, cmds_byte_start);
        get_renderer().staging->copy(pass.instance_buf.get(), ret.gpuinstanceids_vec, 0);

        pass.meshes_vec.clear();
    }
}

MeshRenderer::SetupPassData MeshRenderer::setup(MeshPassType type, RGBuilder& b) const
{
    const auto& pass = m_pass_datas_arr[(int)type];
    SetupPassData data{};
    if(pass.batches_vec.empty()) { return data; }
    data.constants = b.read_buffer(get_renderer().current_data->render_resources.constants);
    data.index = b.import_resource(get_renderer().bufs.indices);
    data.index = b.read_index(data.index);
    data.indirect = b.import_resource(pass.indirect_buf);
    data.indirect = b.read_indirect(data.indirect);
    data.gpuinstances = b.import_resource(pass.instance_buf);
    data.gpuinstances = b.read_buffer(data.gpuinstances);
    return data;
}

void MeshRenderer::draw(MeshPassType type, ICommandBuffer& cmd)
{
    const auto& pass = m_pass_datas_arr[(int)type];
    const auto cmd_size = get_renderer().backend->get_indirect_indexed_command_size();
    cmd.bind_index(get_renderer().bufs.indices.get(), 0ull, VK_INDEX_TYPE_UINT16);
    uint32_t batch_idx = 0;
    for(const auto& batch : pass.batches_vec)
    {
        cmd.bind_pipeline(batch.pipeline.get());
        cmd.draw_indexed_indirect_count(pass.indirect_buf.get(), pass.indirect_cmds_offset + batch.first_command * cmd_size,
                                        pass.indirect_buf.get(), batch_idx * sizeof(uint32_t), batch.command_count, cmd_size);
        ++batch_idx;
    }
}

MeshRenderer::InstancesVec MeshRenderer::extract_mesh_instances(MeshPassType type, PassData& pass)
{
    std::vector<PassData::MeshInstance> instances;
    for(const auto& m : pass.meshes_vec)
    {
        const auto meshlets = m.mesh->geometry->meshlet_range;
        for(auto i = 0u; i < meshlets.size; ++i)
        {
            instances.emplace_back(m.mesh->geometry, m.mesh->material, m.mesh->material->mesh_pass->effects[(int)type]->pipeline,
                                   m.gpu_resource, meshlets.offset + i);
        }
    }
    return instances;
}

void MeshRenderer::sort_mesh_instances(InstancesVec& vec)
{
    // first sort by material (pipeline grouping), then sort by geometry (indirect command per pipeline grouping)
    std::ranges::sort(vec, [](const PassData::MeshInstance& a, const PassData::MeshInstance& b) {
        return std::tie(a.material, a.meshlet) < std::tie(b.material, b.meshlet);
    });
}

MeshRenderer::BuildPassResult MeshRenderer::build_pass_from_instances(PassData& pass, const InstancesVec& vec)
{
    const PassInstancesGroups groups = [&vec] {
        PassInstancesGroups groups;
        groups.reserve(vec.size());
        const PassData::MeshInstance* pi{};
        for(auto i = 0u; i < vec.size(); ++i)
        {
            const auto& ci = vec[i];
            if(!pi || (pi->pipeline != ci.pipeline || pi->meshlet != ci.meshlet))
            {
                groups.emplace_back(i, 1);
                pi = &ci;
                continue;
            }
            else { ++groups.back().size; }
        }
        return groups;
    }();

    pass.batches_vec.clear();
    pass.batches_vec.reserve(vec.size());
    PassData::InstanceBatch* pb{};
    std::vector<std::byte> cmds(groups.size() * get_renderer().backend->get_indirect_indexed_command_size());
    uint32_t cmdoff{};
    std::vector<GPUInstanceId> gpuinstanceids;
    gpuinstanceids.reserve(vec.size());
    for(auto g : groups)
    {
        const auto& fi = vec[g.offset];
        if(!pb || (pb->pipeline != fi.pipeline))
        {
            pass.batches_vec.emplace_back(fi.pipeline, cmdoff, 1);
            pb = &pass.batches_vec.back();
        }
        else { ++pass.batches_vec.back().command_count; }

        const auto& meshlet = get_renderer().meshlets[fi.meshlet];
        get_renderer().backend->make_indirect_indexed_command(cmds.data() + cmdoff * get_renderer().backend->get_indirect_indexed_command_size(),
                                                              meshlet.index_count, g.size, meshlet.index_offset,
                                                              meshlet.vertex_offset, g.offset);
        for(auto i = 0u; i < g.size; ++i)
        {
            gpuinstanceids.push_back(GPUInstanceId{
                .cmdi = cmdoff, .resi = g.offset, .insti = g.offset + i, .mati = *vec[g.offset + i].material });
        }
        ++cmdoff;
    }

    pass.batches_vec.shrink_to_fit();

    return BuildPassResult{ .cmds_vec = std::move(cmds),
                            .gpuinstanceids_vec = std::move(gpuinstanceids),
                            .cmd_count = (uint32_t)groups.size() };
}

} // namespace gfx
} // namespace eng