#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout   : enable
#extension GL_EXT_buffer_reference       : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t transforms_index;
    uint32_t constants_index;
    uint32_t vsm_buffer_index;
    uint32_t page_table_index;
    uint32_t vsm_physical_depth_image_index;
    uint32_t cascade_index;
};

#define NO_PUSH_CONSTANTS
#include "./vsm/common.inc.glsl"

layout(location = 0) in VsOut {
    vec2 vsm_uv;
    float lightProjZ;
    vec3 wpos;
} vsout;

void main() {
    int clip = int(cascade_index);
    uint pageID = vsm_read_virtual_page(vsout.vsm_uv, clip);
    if (vsm_is_alloc_backed(pageID) && vsm_is_alloc_dirty(pageID)) {
        ivec2 phys = vsm_calc_physical_address(vsout.vsm_uv, clip);
        uint zbits = floatBitsToUint(vsout.lightProjZ);
        imageAtomicMin(vsm_pdepth_uint, phys, zbits);
    }
}
