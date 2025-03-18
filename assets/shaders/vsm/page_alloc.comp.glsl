#version 460

#include "./vsm_common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

vec3 unproject_ZO(float depth, vec2 uv, mat4 inv_proj) {
    vec4 ndc = inv_proj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    ndc /= ndc.w;
    return ndc.xyz;
}

vec3 get_world_pos_from_depth_buffer(ivec2 tc) {
    const vec2 texel_size = 1.0 / textureSize(depth_buffer, 0);
    const vec2 texel_center = (vec2(tc) + 0.5) * texel_size;
    const float depth = texelFetch(depth_buffer, tc, 0).x;
    if(depth == 1.0) { return vec3(0.0); }
    return unproject_ZO(depth, texel_center, constants.inv_view * constants.inv_proj);
}

void main() {
    const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(gid, textureSize(depth_buffer, 0)))) { return; }
    vec3 wpos = get_world_pos_from_depth_buffer(gid);
    ivec2 vpi = vsm_calc_page_index(wpos);
    const uint read_val = imageAtomicOr(vsm_page_table, vpi, 1u);
}