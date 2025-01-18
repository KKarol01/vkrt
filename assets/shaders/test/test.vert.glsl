#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer VertexPositionsBuffer {
    vec3 at[]; 
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer IndexBuffer {
    uint32_t at[]; 
};

layout(scalar, push_constant) uniform PushConstants {
    VertexPositionsBuffer vertex_positions;
};

layout(location = 0) out float col;

void main() {
    col = float(gl_VertexIndex);
    gl_Position = vec4(vertex_positions.at[gl_VertexIndex], 1.0);
}