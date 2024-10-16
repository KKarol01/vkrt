#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nor;
layout(location = 2) in vec2 uv;

layout(location = 0) out VertexOutput {
    flat uint mesh_id;
    vec3 pos;
    vec3 nor;
    vec2 uv;
} vert;

#include "../global_common.inc.glsl"
#include "push_constants.inc.glsl"

void main() {
   vec3 tpos = vec3(transforms.at[gl_InstanceIndex] * vec4(pos, 1.0));
   vert.mesh_id = gl_InstanceIndex;
   vert.pos = tpos;
   vert.nor = normalize(vec3(transpose(inverse(transforms.at[gl_InstanceIndex])) * vec4(nor, 0.0)));
   vert.uv = uv;
   gl_Position = globals.proj * globals.view * vec4(tpos, 1.0);
}