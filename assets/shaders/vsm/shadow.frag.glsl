#version 460

#include "./vsm/common.inc.glsl"

layout(location = 0) in VsOut {
    vec3 wpos;
    vec3 light_vpos;
} vsout;

void main() {
    ivec2 vpage_uv = vsm_calc_page_index(vsout.wpos);
    uint vpage = imageLoad(vsm_page_table, vpage_uv).r;
    if(vsm_is_alloc_dirty(vpage) && vsm_is_alloc_backed(vpage)) {
        vec2 vpos_coords = vec2(vsout.light_vpos.xy * float(VSM_VIRTUAL_PAGE_RESOLUTION));
        vec2 ppage_offset = mod(vpos_coords, float(VSM_VIRTUAL_PAGE_RESOLUTION));
        ivec2 ppage_coords = vsm_calc_physical_texel_coords(vpage);
        ivec2 ppos_coords = ppage_coords + ivec2(ppage_offset);
        imageAtomicMin(vsm_pdepth_uint, ppos_coords, floatBitsToUint(vsout.light_vpos.z));
    }
}