#ifndef VSM_COMMON_H
#define VSM_COMMON_H

const float dk = 256.0;
const float md = 200.0;
const mat4 proj_view = mat4(
    2.0 / dk, 0.0, 0.0, 0.0,
    0.0, 2.0 / dk, 0.0, 0.0,
    0.0, 0.0, 1.0 / md, 0.0,
    0.0, 0.0, 0.0, 1.0);

vec3 cam_pos = vec3(0.0, 0.0, 0.0);
mat4 light_view = mat4(1.0);
mat4 vsm_rclip_0_mat = mat4(1.0);

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

#endif