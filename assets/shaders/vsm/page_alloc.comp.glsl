#version 460

#include "./vsm_common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

vec3 unproject_ZO(float depth, vec2 uv, mat4 inv_proj) {
    vec4 ndc = inv_proj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    ndc /= ndc.w;
    return ndc.xyz;
}

vec3 get_world_pos_from_depth_buffer(sampler2D depth_buffer, ivec2 tc, GPUConstants constants) {
    const vec2 texel_size = 1.0 / textureSize(depth_buffer, 0);
    const vec2 texel_center = (vec2(tc) + 0.5) * texel_size;
    const float depth = texelFetch(depth_buffer, tc, 0).x;
    if(depth == 1.0) { return vec3(0.0); }
    return unproject_ZO(depth, texel_center, constants.inv_view * constants.inv_proj);
}

void main() {
    const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(gid, textureSize(GetResource(CombinedImages2D, depth_buffer_index), 0)))) { return; }
    const GPUConstants constants = GetResource(GPUConstantsBuffer, constants_index).constants;
    vsm_rclip_0_mat = GetResource(VsmBuffer, vsm_buffer_index).dir_light_proj * GetResource(VsmBuffer, vsm_buffer_index).dir_light_view;
    vec3 wpos = get_world_pos_from_depth_buffer(GetResource(CombinedImages2D, depth_buffer_index), gid, constants);
    ivec2 vpi = vsm_calc_page_index(wpos, vsm_buffer_index);
    const uint read_val = imageAtomicOr(GetResource(StorageImages2Dr32ui, page_table_index), vpi, 1u);
}