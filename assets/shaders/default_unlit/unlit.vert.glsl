#version 460

#include "./common.inc.glsl"

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
    gl_Position = constants.proj_view * vec4(vsout.position, 1.0);
}