#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"

layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t vertex_attributes_index;
    uint32_t constants_index;
    uint32_t mesh_instances_index;
    uint32_t transform_buffer_index;
};

layout(location = 0) out VsOut {
    vec3 normal;
    vec3 tangent;
    vec2 uv;
    flat uint32_t instance_index;
} vsout;

void main() {
    Vertex vertex = get_vertex(vertex_positions_index, vertex_attributes_index, gl_VertexIndex);
    vsout.normal = vertex.normal;
    vsout.tangent = vertex.tangent;
    vsout.uv = vertex.uv;
    vsout.instance_index = gl_InstanceIndex;

    GPUConstants constants = GetResource(GPUConstantsBuffer, constants_index).constants;
    gl_Position = constants.proj * (constants.view * (GetResource(GPUTransformsBuffer, transform_buffer_index).at[gl_InstanceIndex] * vec4(vertex.position, 1.0)));
}