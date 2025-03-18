#version 460

#include "./vsm_common.inc.glsl"

void main() {
    gl_Position = constants.proj * constants.view * transforms[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0);
}