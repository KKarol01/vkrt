#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    flat uint32_t instance_index;
} vsout;

void main() {
    vsout.position = vec3(transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0));
    vsout.normal = attrib_pos_arr[gl_VertexIndex].normal;
    vsout.uv = attrib_pos_arr[gl_VertexIndex].uv;
    vsout.tangent = attrib_pos_arr[gl_VertexIndex].tangent.xyz * attrib_pos_arr[gl_VertexIndex].tangent.w;
    vsout.instance_index = gl_InstanceIndex;
    vec2 disp = imageLoad(storageImages_2drg32f[fft_fourier_amplitudes_index], ivec2(vsout.uv * imageSize(storageImages_2drg32f[fft_fourier_amplitudes_index]).xy)).rg;
    gl_Position = constants.proj_view * vec4(vsout.position, 1.0);
}