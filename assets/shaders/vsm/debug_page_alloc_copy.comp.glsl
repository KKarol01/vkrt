#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

// assuming storage
layout(scalar, push_constant) uniform PushConstants {
    uint32_t src_index; // storage
    uint32_t dst_index; // storage
};

void main() {
    const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(gid, ivec2(64)))) { return; }
    imageStore(GetResource(StorageImages2Drgba8, dst_index), gid,
               vec4(imageLoad(GetResource(StorageImages2Dr32ui, src_index), gid).r, 0.0, 0.0, 1.0));
}