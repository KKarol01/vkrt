#ifndef VSM_COMMON_H
#define VSM_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.inc.glsl"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t vertex_attributes_index;
    uint32_t transforms_index;
    uint32_t vsm_buffer_index;
    uint32_t vsm_alloc_index;
    uint32_t depth_buffer_index;
    uint32_t page_table_index;
    uint32_t constants_index;
    uint32_t use_vsm;
    uint32_t vsm_physical_depth_image_index;
};
#endif

#define vsm_constants storageBuffers_GPUVsmConstantsBuffer[vsm_buffer_index]
#define vsm_alloc_constants storageBuffers_GPUVsmAllocConstantsBuffer[vsm_alloc_index]
#define vsm_page_table storageImages_2dr32uiArray[page_table_index]
#define vsm_pdepth_uint storageImages_2dr32ui[vsm_physical_depth_image_index]

#define depth_buffer combinedImages_2d[depth_buffer_index]

#define VSM_INVALID_PAGE_TEXEL ivec2(-1, -1)
// for virtual page (32bit int entry per page) decoding
// if set - redo shadow mapping
#define VSM_DIRTY_FLAG       0x1
// if set - is backed by physical allocation
#define VSM_BACKED_FLAG      0x2
// for virtual pages of size 128x128, physical page of size 16Kx16K needs 7 bit indices
// 8 bit number mask for locating position within physical memory
#define VSM_PPAGE_X_POS_MASK (0xFF << 2)
// 8 bit number mask for locating position within physical memory
#define VSM_PPAGE_Y_POS_MASK (0xFF << 10)

#define vsm_is_alloc_dirty(alloc)    ((alloc & VSM_DIRTY_FLAG) > 0)
#define vsm_is_alloc_backed(alloc)   ((alloc & VSM_BACKED_FLAG) > 0)
#define vsm_get_alloc_ppos_x(alloc)  ((alloc & VSM_PPAGE_X_POS_MASK) >> 2)
#define vsm_get_alloc_ppos_y(alloc)  ((alloc & VSM_PPAGE_Y_POS_MASK) >> 10)

vec3 vsm_clip0_to_clip_n(vec3 o, int clip_index) { return vec3(o.xy * vec2(1.0 / float(1 << clip_index)), o.z); }

vec3 vsm_calc_rclip(vec3 world_pos, int clip_index) {
    return vec3(vsm_clip0_to_clip_n(vec3(vsm_constants.dir_light_proj_view[0] * vec4(world_pos, 1.0)), clip_index));
}

vec3 vsm_calc_sclip(vec3 world_pos, int clip_index) {
    vec3 res = vsm_calc_rclip(world_pos, clip_index);
    return res - vsm_clip0_to_clip_n(vsm_constants.dir_light_proj_view[0][3].xyz, clip_index);
}

int vsm_calc_clip_index(vec3 world_pos) {
    //const float fcw = VSM_NUM_VIRTUAL_PAGES * VSM_VIRTUAL_PAGE_RESOLUTION / ()
    float clip = distance(world_pos, constants.cam_pos) / VSM_CLIP0_LENGTH;
    clip = ceil(log2(clip) + 2.3);
    return int(clamp(clip, 0, VSM_NUM_CLIPMAPS - 1));
}

struct VirtualPageAddress {
    ivec3 addr;
    vec3 ndc;
};
VirtualPageAddress vsm_calc_virtual_page_texel(vec3 world_pos) {
    int clip_index = vsm_calc_clip_index(world_pos);
    vec4 posc = vsm_constants.dir_light_proj_view[clip_index] * vec4(world_pos, 1.0);
    posc = posc - vsm_constants.dir_light_proj_view[clip_index] * vec4(vsm_constants.dir_light_proj_view[clip_index][3].xyz, 1.0);
    posc.xyz = vsm_calc_sclip(world_pos, clip_index);
    //posc /= posc.w;
    //posc.xy *= 1.0 / float(1 << clip_index);
    vec2 posuv = fract(posc.xy * 0.5 + 0.5);
    ivec2 page_texel = ivec2(posuv * VSM_NUM_VIRTUAL_PAGES);
    VirtualPageAddress addr;
    addr.addr = ivec3(page_texel, clip_index);
    addr.ndc = vec3(posc.xy, posc.z);
    return addr;
}

ivec2 vsm_calc_physical_page_texel(vec3 world_pos) {
    VirtualPageAddress addr = vsm_calc_virtual_page_texel(world_pos);
    uint vpage = imageLoad(vsm_page_table, addr.addr).r;
    if(vsm_is_alloc_backed(vpage)) {
        vec2 vpos_coords = vec2((addr.ndc.xy * 0.5 + 0.5) * float(VSM_PHYSICAL_PAGE_RESOLUTION));
        vec2 ppage_offset = mod(vpos_coords, float(VSM_VIRTUAL_PAGE_RESOLUTION));
        ivec2 ppage_coords = ivec2(vsm_get_alloc_ppos_x(vpage), vsm_get_alloc_ppos_y(vpage)) * VSM_VIRTUAL_PAGE_RESOLUTION;
        ivec2 ppos_coords = ppage_coords + ivec2(ppage_offset);
        return ppos_coords;
    }
    return VSM_INVALID_PAGE_TEXEL;
}
//vec2 vsm_calc_virtual_coords(vec3 world_pos) { // todo: maybe rename to virtual_uvs
//    const float clip_idx = clamp(max(0.0, ceil(log2(distance(world_pos, constants.cam_pos) / VSM_CLIP0_LENGTH))), 0.0, float(VSM_NUM_CLIPMAPS - 1));
//    vec2 vtc = vsm_calc_sclip(world_pos, int(clip_idx)).xy;
//    vtc = fract(vtc * 0.5 + 0.5);
//    return vtc;
//}
//
//
//ivec2 vsm_calc_physical_texel_coords(uint vpage) {
//    return ivec2(vsm_get_alloc_ppos_x(vpage) * VSM_VIRTUAL_PAGE_RESOLUTION, vsm_get_alloc_ppos_y(vpage) * VSM_VIRTUAL_PAGE_RESOLUTION);
//    //const vec2 vtc = vsm_calc_virtual_coords(world_pos);
//    //const vec2 page_start_vtc = fract(vec2(page_index) / vec2(vsm_constants.num_pages_xy));
//    //return fract(vtc - page_start_vtc);
//}

#endif