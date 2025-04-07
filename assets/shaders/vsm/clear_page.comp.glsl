#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
layout(scalar, push_constant) uniform PushConstants {
    uint32_t vsm_alloc_index;
    uint32_t page_table_index;
    uint32_t vsm_buffer_index; // unused, but required
};
#define NO_PUSH_CONSTANTS
#include "./vsm/common.inc.glsl"


layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    if(all(equal(gl_GlobalInvocationID, uvec3(0)))) { vsm_alloc_constants.free_list_head = 0; }
    const int idx = int(gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_WorkGroupSize.x * gl_NumWorkGroups.x);
    vsm_alloc_constants.free_list[idx] = idx;

    if(gl_GlobalInvocationID.x >= 64 || gl_GlobalInvocationID.y >= 64) { return; }
    imageStore(vsm_page_table, ivec2(gl_GlobalInvocationID.xy), ivec4(0));
}