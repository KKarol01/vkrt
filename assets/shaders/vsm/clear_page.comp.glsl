#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(scalar, push_constant) uniform PushConstants {
    uint32_t page_table_index;
	uint32_t texel_resolution_xy;
};

void main() {
	if(gl_GlobalInvocationID.x >= texel_resolution_xy || gl_GlobalInvocationID.y >= texel_resolution_xy) { return; }
	imageStore(GetResource(StorageImages2Dr32ui, page_table_index), ivec2(gl_GlobalInvocationID.xy), ivec4(0));
}