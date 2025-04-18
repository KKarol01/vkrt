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
    vec4 posc = vsm_constants.dir_light_proj_view[clip_index] * vec4(world_pos, 1.0);
    posc /= posc.w;
    //return posc.xyz;
    return vsm_clip0_to_clip_n(posc.xyz, 0);
}

vec3 vsm_calc_sclip(vec3 world_pos, int clip_index) {
    vec4 posc = vsm_constants.dir_light_proj_view[clip_index] * vec4(world_pos, 1.0);
    posc /= posc.w;
    return posc.xyz;
}

int vsm_calc_clip_index(vec3 world_pos) {
    float clip = exp(distance(world_pos, constants.cam_pos)) / VSM_CLIP0_LENGTH;
    clip = ceil(log2(clip));
    return int(clamp(clip, 0.0, float(VSM_NUM_CLIPMAPS - 1)));
}

vec2 vsm_calc_virtual_uv(vec3 wpos, int clip_index) {
    vec3 ndc = vsm_calc_sclip(wpos, clip_index);
    return fract(ndc.xy * 0.5 + 0.5);
}

ivec3 vsm_calc_virtual_page_address(vec2 uv, int clip_index) {
    return ivec3(uv * imageSize(vsm_page_table).xy, clip_index);
}

uint vsm_read_virtual_page(vec2 uv, int clip_index) {
    ivec3 coords = vsm_calc_virtual_page_address(uv, clip_index);
    return imageLoad(vsm_page_table, coords).r;
}

ivec2 vsm_calc_physical_address(vec2 uv, int clip_index) {
    uint page = vsm_read_virtual_page(uv, clip_index);
    if(!vsm_is_alloc_backed(page)) { return VSM_INVALID_PAGE_TEXEL; }
    vec2 page_pos_xy = vec2(vsm_get_alloc_ppos_x(page), vsm_get_alloc_ppos_y(page)) * VSM_VIRTUAL_PAGE_PHYSICAL_SIZE;
    vec2 in_page_offset = mod(uv * vec2(VSM_PHYSICAL_PAGE_RESOLUTION), VSM_VIRTUAL_PAGE_PHYSICAL_SIZE);
    return ivec2(page_pos_xy + in_page_offset);
}

float vsm_get_depth(vec2 uv, int clip_index) {
    if(clip_index >= VSM_NUM_CLIPMAPS) { return 1.0; }
    ivec2 coords = vsm_calc_physical_address(uv, clip_index);
    if(coords == VSM_INVALID_PAGE_TEXEL) { return 1.0; }
    return uintBitsToFloat(imageLoad(vsm_pdepth_uint, coords).r);
}

#endif