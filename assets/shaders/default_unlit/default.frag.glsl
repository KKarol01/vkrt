#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(location = 0) out vec4 FRAG_COL;

layout(location = 0) in VertexOutput {
    flat uint mesh_id;
    vec2 uv;
} vert;

#include "../common.inc.glsl"
#include "push_constants.inc.glsl"

layout(binding = 15, set = 0) uniform sampler2D textures[];

void main() {
	MeshData md = mesh_datas.at[vert.mesh_id];

	FRAG_COL = vec4(texture(textures[nonuniformEXT(md.color_texture)], vert.uv).rgba);
}