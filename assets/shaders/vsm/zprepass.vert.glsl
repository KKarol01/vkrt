#version 460

#include "./vsm_common.inc.glsl"

void main() {
    gl_Position = GetResource(GPUConstantsBuffer, constants_index).constants.proj *
                  (GetResource(GPUConstantsBuffer, constants_index).constants.view *
                   (GetResource(GPUTransformsBuffer, transform_buffer_index).at[gl_InstanceIndex] *
                    vec4(get_vertex_position(vertex_positions_index, gl_VertexIndex), 1.0)));
}