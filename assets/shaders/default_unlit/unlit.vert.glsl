#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    flat uint32_t instance_index;
    vec3 water_normal;
} vsout;

void main() {
    vsout.position = vec3(transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0));
    vsout.normal = attrib_pos_arr[gl_VertexIndex].normal;
    vsout.uv = attrib_pos_arr[gl_VertexIndex].uv;
    vsout.tangent = attrib_pos_arr[gl_VertexIndex].tangent.xyz * attrib_pos_arr[gl_VertexIndex].tangent.w;
    vsout.instance_index = gl_InstanceIndex;
    float disp =  textureLod(combinedImages_2d[fft_fourier_amplitudes_index], vsout.uv, 0.0).r;
    float dispx = textureLod(combinedImages_2d[fft_fourier_x_amplitudes_index], vsout.uv, 0.0).r;
    float dispz = textureLod(combinedImages_2d[fft_fourier_z_amplitudes_index], vsout.uv, 0.0).r;
    vec3 wn = textureLod(combinedImages_2d[fft_fourier_n_amplitudes_index], vsout.uv, 0.0).rgb;
    vsout.position.xyz += vec3(dispx, disp, dispz) * 0.3;
    vsout.water_normal = wn;
    gl_Position = constants.proj_view * vec4(vsout.position, 1.0);
}