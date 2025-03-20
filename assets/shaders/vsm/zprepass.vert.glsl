#version 460

#include "./vsm/common.inc.glsl"

void main() {
    gl_Position = constants.proj_view * transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0);
}