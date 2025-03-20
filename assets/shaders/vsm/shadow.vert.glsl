#version 460

#include "./vsm/common.inc.glsl"

void main() {
    vec4 vertex = transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0);
    vec4 proj_pos = vsm_constants.dir_light_proj * vsm_constants.dir_light_view * vertex;
    gl_Position = proj_pos;

#if 0
    vsm_rclip_0_mat = proj_view * light_view;
    vec3 lspos = vsm_calc_rclip(vsout.position, 0);
    vec2 clip_index2 = vec2(
        ceil(log2(max(abs(lspos.x), 1.0))),
        ceil(log2(max(abs(lspos.y), 1.0)))
    );
    int clip_index = 0; int(max(clip_index2.x, clip_index2.y));
    lspos = vsm_calc_sclip(vsout.position, clip_index);
    lspos.xy = lspos.xy;
    //lspos.xy = fract(lspos.xy);
    vec2 vtc = lspos.xy;
    //ivec2 pti = ivec2(floor(vtc * vec2(vsmconsts.num_pages_xy)));
    gl_Position = vec4(vtc, lspos.z, 1.0);
#endif
}