#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(scalar, push_constant) uniform PushConstants {
    uint32_t src_image_index;
    uint32_t dst_image_index;
    uint32_t vsm_buffer_index;
    uint32_t constants_index;  // unused
    uint32_t page_table_index; // unused
};
#define NO_PUSH_CONSTANTS
#include "./vsm/common.inc.glsl"

#define src_image storageImages_2dr32uiArray[src_image_index]
#define dst_image storageImages_2drgba8Array[dst_image_index]

layout(local_size_x = 8, local_size_y = 8) in;

void main() {
    const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(gid, ivec2(VSM_NUM_VIRTUAL_PAGES)))) { return; }
    imageStore(dst_image, ivec3(gid, 0), vec4(imageLoad(src_image, ivec3(gid, 0)).r, 0.0, 0.0, 1.0));
    imageStore(dst_image, ivec3(gid, 1), vec4(imageLoad(src_image, ivec3(gid, 1)).r, 0.0, 0.0, 1.0));
}