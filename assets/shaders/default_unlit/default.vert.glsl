#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nor;
layout(location = 2) in vec2 uv;

layout(location = 0) flat out uint vmesh_id;
layout(location = 1) out vec2 vuv;

#include "../rtbasic/push_constants.inc"

layout(binding = 14, set = 0) uniform CameraProperties {
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation;
} cam;

void main() {
    uint triangle_id = (gl_VertexIndex + 3 - gl_VertexIndex % 3) / 2;
    uint mesh_id = combined_rt_buffs.mesh_ids.ids[triangle_id + combined_rt_buffs.offsets.offsets[gl_InstanceIndex]];
    vmesh_id = mesh_id;
    vuv = uv;
	gl_Position = inverse(cam.projInverse) * inverse(cam.viewInverse) * vec4(pos, 1.0);
}