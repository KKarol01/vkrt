#ifndef VSM_COMMON_H
#define VSM_COMMON_H

#include "./bindless_structures.inc.glsl"

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t transforms_index;
    uint32_t vsm_buffer_index;
    uint32_t vsm_alloc_index;
    uint32_t depth_buffer_index;
    uint32_t page_table_index;
    uint32_t constants_index;
};
#endif

#define vsm_constants u_storageBuffers_GPUVsmConstantsBuffer[vsm_buffer_index]
#define vsm_alloc_constants u_storageBuffers_GPUVsmConstantsBuffer[vsm_buffer_index]

vec3 vsm_clip0_to_clip_n(vec3 o, int clip_index) { return vec3(o.xy * vec2(1.0 / float(1 << clip_index)), o.z); }

vec3 vsm_calc_rclip(vec3 world_pos, int clip_index) {
    return vec3(vsm_clip0_to_clip_n(vec3(vsm_rclip_0_mat * vec4(world_pos, 1.0)), clip_index));
}

vec3 vsm_calc_sclip(vec3 world_pos, int clip_index) {
    vec3 res = vsm_calc_rclip(world_pos, clip_index);
    return res - vsm_clip0_to_clip_n(vsm_rclip_0_mat[3].xyz, clip_index);
}

vec2 vsm_calc_virtual_coords(vec3 world_pos) {
    vec2 vtc = vsm_calc_sclip(world_pos, 0).xy;
    vtc = fract(vtc * 0.5 + 0.5);
    return vtc;
}

ivec2 vsm_calc_page_index(vec3 world_pos, uint vsm_buffer_index) {
    vec2 vtc = vsm_calc_virtual_coords(world_pos);
    return ivec2(floor(vtc * GetResource(VsmBuffer, vsm_buffer_index).num_pages_xy));
}

vec2 vsm_calc_page_uv(vec3 world_pos, ivec2 page_index, uint vsm_buffer_index) {
    const vec2 vtc = vsm_calc_virtual_coords(world_pos);
    const vec2 page_start_vtc = fract(vec2(page_index) / vec2(GetResource(VsmBuffer, vsm_buffer_index).num_pages_xy));
    return fract(vtc - page_start_vtc);
}

#endif