#version 460

#extension GL_KHR_shader_subgroup_ballot : require

#include "./vsm/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

#define EPS 1e-4

vec3 unproject_ZO(float depth, vec2 uv) {
    vec4 ndc = constants.inv_proj_view * vec4(uv * 2.0 - 1.0, depth, 1.0);
    ndc /= ndc.w;
    return ndc.xyz;
}

vec3 get_world_pos_from_depth_buffer(ivec2 tc, out float depth) {
    const vec2 texel_size = 1.0 / textureSize(depth_buffer, 0);
    const vec2 texel_center = (vec2(tc) + 0.5) * texel_size;
    depth = texelFetch(depth_buffer, tc, 0).x;
    return unproject_ZO(depth, texel_center);
}

ivec2 try_allocate_physical_page() {
    if(vsm_alloc_constants.free_list_head >= VSM_MAX_ALLOCS) { return ivec2(-1, -1); }
    uint head = atomicAdd(vsm_alloc_constants.free_list_head, 1u);
    return ivec2(head % vsm_constants.num_pages_xy, head / vsm_constants.num_pages_xy);
}

void main() {
    const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(gid, textureSize(depth_buffer, 0)))) { return; }
    float depth;
    vec3 wpos = get_world_pos_from_depth_buffer(gid, depth);
    if(depth > 1.0 - EPS) { return; }
    ivec2 vpi = vsm_calc_page_index(wpos);

    for(;;) {
        ivec2 brd_vpi = subgroupBroadcastFirst(vpi); // match until your address gets broadcasted
        if(brd_vpi == vpi && subgroupElect()) {      // amongst matched ones, prefer only one to do atomic ops
            imageAtomicOr(vsm_page_table, vpi, VSM_DIRTY_FLAG);
            const uint page_bits = imageAtomicOr(vsm_page_table, vpi, VSM_BACKED_FLAG);
            if(!vsm_is_alloc_backed(page_bits)) {
                const ivec2 paddr = try_allocate_physical_page();
                if(paddr == ivec2(-1, -1)) { break; }
                uint page_table_entry = ((paddr.y & 0xFF) << 10) | ((paddr.x & 0xFF) << 2) | (VSM_BACKED_FLAG | VSM_DIRTY_FLAG);
                imageAtomicCompSwap(vsm_page_table, vpi, VSM_BACKED_FLAG | VSM_DIRTY_FLAG, page_table_entry);
            }
            break;
        }
    }
}