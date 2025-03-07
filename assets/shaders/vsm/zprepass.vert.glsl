#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"

layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t constants_index;
    uint32_t vertex_positions_index;
    uint32_t transform_buffer_index;
};

void main() {
    gl_Position = GetResource(GPUConstantsBuffer, constants_index).constants.proj *
                  (GetResource(GPUConstantsBuffer, constants_index).constants.view *
                   (GetResource(GPUTransformsBuffer, transform_buffer_index).at[gl_InstanceIndex] *
                    vec4(get_vertex_position(vertex_positions_index, gl_VertexIndex), 1.0)));
}