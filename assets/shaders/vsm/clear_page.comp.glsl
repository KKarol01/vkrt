#version 460

#include "./vsm/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
	if(gl_GlobalInvocationID.x >= 64 || gl_GlobalInvocationID.y >= 64) { return; }
	imageStore(vsm_page_table, ivec2(gl_GlobalInvocationID.xy), ivec4(0));
}