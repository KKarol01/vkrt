#version 460

#include "./vsm/common.inc.glsl"

layout(location = 0) in VsOut {
    vec3 wpos;
} vsout;

void main() {
    VirtualPageAddress addr = vsm_calc_virtual_page_texel(vsout.wpos);
    ivec3 vpage_uv = addr.addr;
    uint vpage = imageLoad(vsm_page_table, vpage_uv).r;
    if(vsm_is_alloc_dirty(vpage) && vsm_is_alloc_backed(vpage)) {
        ivec2 ppos_coords = vsm_calc_physical_page_texel(vsout.wpos);
        imageAtomicMin(vsm_pdepth_uint, ppos_coords, floatBitsToUint(gl_FragCoord.z));
    }
}