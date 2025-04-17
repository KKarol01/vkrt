#version 460

#include "./vsm/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    if(all(equal(gl_GlobalInvocationID, uvec3(0)))) { vsm_alloc_constants.free_list_head = 0; }
    const int idx = int(gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_WorkGroupSize.x * gl_NumWorkGroups.x);
    // vsm_alloc_constants.free_list[idx] = idx;

    if(gl_GlobalInvocationID.x >= VSM_VIRTUAL_PAGE_RESOLUTION || gl_GlobalInvocationID.y >= VSM_VIRTUAL_PAGE_RESOLUTION) { return; }
    for(int i = 0; i < VSM_NUM_CLIPMAPS; ++i) {
        imageStore(vsm_page_table, ivec3(gl_GlobalInvocationID.xy, i), ivec4(0));
    }
}