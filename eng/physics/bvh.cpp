#include "bvh.hpp"
#include <eng/renderer/renderer_fwd.hpp>

namespace eng
{
namespace physics
{
BVH::BVH(std::span<const std::byte> vertices, size_t stride, std::span<const std::byte> indices, gfx::IndexFormat index_format)
{
    return;
    // make sure vertices are non empty and the stride at least is one position (3 floats.
    assert(vertices.size() > 0 && stride >= 3 * sizeof(float));

    const auto ic = gfx::get_index_count(indices, index_format);
    // if using indices, make sure they form triangles; or check if vertices form triangles
    assert(ic > 0 ? ic % 3 == 0 : (vertices.size() / stride) % 3 == 0);

    std::vector<uint32_t> ids(ic);
    if(ic > 0)
    {
        gfx::copy_indices(std::as_writable_bytes(std::span{ ids }), std::span{ indices }, gfx::IndexFormat::U32, index_format);
    }

    // copy vertex positions to triangles
    const auto tri_count = (ic > 0 ? ic : vertices.size() / stride) / 3;
    tris.resize(tri_count);
    const auto* psrc = vertices.data();
    for(auto i = 0ull; i < tri_count; ++i)
    {
        const auto* pa = ic > 0 ? psrc + ids[i * 3 + 0] * stride : psrc + (i * 3 + 0) * stride;
        const auto* pb = ic > 0 ? psrc + ids[i * 3 + 1] * stride : psrc + (i * 3 + 1) * stride;
        const auto* pc = ic > 0 ? psrc + ids[i * 3 + 2] * stride : psrc + (i * 3 + 2) * stride;
        memcpy(&tris[i].a, pa, sizeof(tris[i].a));
        memcpy(&tris[i].b, pb, sizeof(tris[i].b));
        memcpy(&tris[i].c, pc, sizeof(tris[i].c));
    }

    assert(tris.size() <= UINT32_MAX);

    nodes.reserve(tri_count * 2 - 1);
    nodes.push_back(Node{ .aabb = {}, .left_or_pstart = 0, .pcount = (uint32_t)tris.size() });
    update_bounds(0);
    subdivide(0);
    nodes.shrink_to_fit();
    metadatas.resize(nodes.size());

    const auto count_levels = [this](uint32_t node, uint32_t level, const auto& self) -> uint32_t {
        const auto& n = nodes[node];
        metadatas[node].level = level;
        if(n.is_leaf()) { return level; }
        return std::max(self(n.left_or_pstart, level + 1, self), self(n.left_or_pstart + 1, level + 1, self));
    };
    levels = count_levels(0, 1, count_levels);
}

BVH::Stats BVH::get_stats() const
{
    return Stats{
        .size = nodes.size() * sizeof(nodes[0]) + tris.size() * sizeof(tris[0]),
        .levels = levels,
        .tris = tris,
        .nodes = nodes,
        .metadatas = metadatas,
    };
}

void BVH::subdivide(uint32_t node)
{
    auto& n = nodes[node];
    if(n.pcount <= 2) { return; }
    int axis = 0;
    const auto extent = n.aabb.extent();
    if(extent.y > extent[axis]) { axis = 1; }
    if(extent.z > extent[axis]) { axis = 2; }
    const auto splitpos = (n.aabb.min[axis] + n.aabb.max[axis]) * 0.5f;

    uint32_t a = n.left_or_pstart;
    uint32_t b = a + n.pcount - 1;
    while(a <= b)
    {
        const auto p = tris[a].centroid()[axis];
        if(p < splitpos) { ++a; }
        else { std::swap(tris[a], tris[b--]); }
    }

    // don't subdivide if one child has all all the triangles
    if(a - n.left_or_pstart == 0 || a - n.left_or_pstart == n.pcount) { return; }

    const auto lni = nodes.size();
    nodes.emplace_back();
    nodes.emplace_back();
    nodes[lni].left_or_pstart = n.left_or_pstart;
    nodes[lni].pcount = a - n.left_or_pstart;
    nodes[lni + 1].left_or_pstart = a;
    nodes[lni + 1].pcount = n.pcount - nodes[lni].pcount;
    n.left_or_pstart = lni;
    n.pcount = 0;
    update_bounds(lni);
    update_bounds(lni + 1);
    subdivide(lni);
    subdivide(lni + 1);
}

void BVH::update_bounds(uint32_t node)
{
    auto& n = nodes[node];
    n.aabb = {};
    for(auto i = 0u; i < n.pcount; ++i)
    {
        n.aabb.min = glm::min(tris[n.left_or_pstart + i].a, n.aabb.min);
        n.aabb.min = glm::min(tris[n.left_or_pstart + i].b, n.aabb.min);
        n.aabb.min = glm::min(tris[n.left_or_pstart + i].c, n.aabb.min);
        n.aabb.max = glm::max(tris[n.left_or_pstart + i].a, n.aabb.max);
        n.aabb.max = glm::max(tris[n.left_or_pstart + i].b, n.aabb.max);
        n.aabb.max = glm::max(tris[n.left_or_pstart + i].c, n.aabb.max);
    }
}

} // namespace physics
} // namespace eng