#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    flat uint32_t instance_index;
    vec3 water_normal;
     float ndc_bs;
}
vsout;

void main() {
    vec3 pos = vertex_pos_arr[gl_VertexIndex];
    vec4 vpos = constants.proj_view * vec4(pos, 1.0);

    GPUInstanceId id = meshlet_ids[culled_ids[gl_InstanceIndex]];
    vec4 bs = meshlets_bs[culled_ids[gl_InstanceIndex]];

    vec4 view_bs = constants.view * vec4(bs.xyz, 1.0);
    view_bs.z -= bs.w;
    vec4 ndc_bs = constants.proj * view_bs;
    vsout.ndc_bs = ndc_bs.z / ndc_bs.w;
    vsout.ndc_bs = vsout.ndc_bs * 0.5 + 0.5;
    vsout.instance_index = uint(culled_ids[gl_InstanceIndex] * 0x6F7DEF7);
    gl_Position = vpos;

    // vsout.position = vec3(transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0));
    // vsout.normal = attrib_pos_arr[gl_VertexIndex].normal;
    // vsout.uv = attrib_pos_arr[gl_VertexIndex].uv;
    // vsout.tangent = attrib_pos_arr[gl_VertexIndex].tangent.xyz * attrib_pos_arr[gl_VertexIndex].tangent.w;
    // vsout.instance_index = gl_InstanceIndex;
    // vec3 disp = textureLod(combinedImages_2d[fft_displacement_index], vsout.uv, 0.0).rgb;
    // vsout.position += disp;
    // vsout.water_normal = disp;
    // gl_Position = constants.proj_view * vec4(vsout.position, 1.0);
}