#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"

layout(scalar, push_constant) uniform PushConstants {
    uint32_t vertex_positions_index;
    uint32_t constants_index;
};

layout(location = 0) out vec3 pos;

void main() {
    pos = GetResource(GPUVertexPositionsBuffer, vertex_positions_index).at[gl_VertexIndex];
    gl_Position = vec4(pos, 1.0);
    GPUConstants constants = GetResource(GPUConstantsBuffer, constants_index).constants;
    gl_Position = constants.proj * constants.view * gl_Position;
}