#include "renderpass.hpp"
#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/math/align.hpp>

namespace eng
{
namespace gfx
{

void MeshRenderData::build()
{
    if(!mesh_instances.empty()) { return; }

    mesh_instances.clear();
    draw.batches.clear();

    auto& r = get_renderer();
    const auto rpidx = (int)type;

    for(const auto& m : meshes)
    {
        for(auto i = 0u; i < m.geometry->meshlet_range.size; ++i)
        {
            const auto mltidx = m.geometry->meshlet_range.offset + i;
            mesh_instances.push_back(MeshInstance{
                .geometry = m.geometry,
                .material = m.material,
                .instance_index = m.instance_index,
                .meshlet_index = mltidx,
            });
        }
    }

    if(mesh_instances.empty()) { return; }

    std::sort(mesh_instances.begin(), mesh_instances.end(), [](const MeshInstance& a, const MeshInstance& b) {
        return std::tie(a.material, a.meshlet_index) < std::tie(b.material, b.meshlet_index);
    });

    std::vector<GPUInstanceId> insts;
    std::vector<IndexedIndirectDrawCommand> cmds;
    std::vector<uint32_t> counts;
    insts.reserve(mesh_instances.size());
    cmds.reserve(mesh_instances.size());
    counts.reserve(mesh_instances.size());
    Handle<Pipeline> prev_pipeline;
    uint32_t prev_meshlet = ~0u;
    for(auto i = 0u; i < mesh_instances.size(); ++i)
    {
        const auto& inst = mesh_instances[i];
        const auto& mat = inst.material.get();
        const auto& mp = mat.mesh_pass->effects[rpidx].get();
        if(prev_pipeline != mp.pipeline)
        {
            prev_pipeline = mp.pipeline;
            draw.batches.push_back(InstanceBatch{
                .pipeline = mp.pipeline, .instance_count = 0, .first_command = (uint32_t)cmds.size(), .command_count = 0 });
            counts.push_back(0);
        }
        if(prev_meshlet != inst.meshlet_index)
        {
            prev_meshlet = inst.meshlet_index;
            const auto& mlt = r.meshlets[inst.meshlet_index];
            cmds.push_back(IndexedIndirectDrawCommand{ .indexCount = mlt.index_count,
                                                       .instanceCount = 0,
                                                       .firstIndex = mlt.index_offset,
                                                       .vertexOffset = mlt.vertex_offset,
                                                       .firstInstance = i });
        }
        insts.push_back(GPUInstanceId{
            .cmdi = (uint32_t)cmds.size(), .resi = (uint32_t)insts.size(), .insti = inst.instance_index, .mati = *inst.material });
        ++draw.batches.back().instance_count;
        ++cmds.back().instanceCount;
        draw.batches.back().command_count = cmds.size() - draw.batches.back().first_command;
        counts.back() = draw.batches.back().command_count;
    }

    const auto cnts_size = counts.size() * sizeof(counts[0]);
    const auto cmds_start = align_up2(cnts_size, 16);
    const auto cmds_size = cmds.size() * r.backend->get_indirect_indexed_command_size();
    const auto total_size = cmds_start + cmds_size;
    if(!draw.indirect_buf)
    {
        draw.indirect_buf =
            r.make_buffer("indirect buffer", Buffer::init(total_size, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT));
    }
    else { r.resize_buffer(draw.indirect_buf, total_size, false); }

    std::vector<std::byte> backendcmds(cmds_size);
    for(auto i = 0ull; i < cmds.size(); ++i)
    {
        r.backend->make_indirect_indexed_command(&backendcmds[i * r.backend->get_indirect_indexed_command_size()],
                                                 cmds[i].indexCount, cmds[i].instanceCount, cmds[i].firstIndex,
                                                 cmds[i].vertexOffset, cmds[i].firstInstance);
    }
    r.staging->copy(draw.indirect_buf, counts, 0ull, false);
    r.staging->copy(draw.indirect_buf, backendcmds, cmds_start, false);

    draw.counts_view = BufferView::init(draw.indirect_buf, 0ull, cnts_size);
    draw.cmds_view = BufferView::init(draw.indirect_buf, cmds_start, cmds_size);
}

void MeshRenderData::add_mesh(uint32_t instance_index, Handle<gfx::Mesh> mesh)
{
    mesh_instances.clear();
    const auto& m = mesh.get();
    MeshInstance mi{};
    mi.material = m.material;
    mi.geometry = m.geometry;
    mi.instance_index = instance_index;
    meshes.push_back(mi);
}

void IndirectBatch::draw(const Callback<void(const IndirectDrawParams&)>& draw_callback) const
{
    size_t cmdoffacc = 0;
    for(auto i = 0u; i < batches.size(); ++i)
    {
        const auto& batch = batches[i];
        const auto cntoff = sizeof(uint32_t) * i;
        const auto cmdsize = get_renderer().backend->get_indirect_indexed_command_size();
        const auto cmdoff = cmdsize * cmdoffacc + cmds_view.range.offset;
        draw_callback(IndirectDrawParams{
            .draw = &batch,
            .max_draw_count = batch.command_count,
            .stride = cmdsize,
            .command_offset_bytes = cmdoff,
            .count_offset_bytes = cntoff,
        });
        cmdoffacc += batch.command_count;
    }
}

} // namespace gfx
} // namespace eng