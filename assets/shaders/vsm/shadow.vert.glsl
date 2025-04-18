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

layout(location = 0) out VsOut {
    vec2 vsm_uv;
    float lightProjZ;
    vec3 wpos;
} vsout;

void main() {
    vec4 worldPos = transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0);
    vsout.wpos = worldPos.xyz;
    vec4 lightPos = vsm_constants.dir_light_proj_view[cascade_index] * worldPos;
    vsout.vsm_uv     = clamp(lightPos.xy / lightPos.w * 0.5 + 0.5, vec2(0.0), vec2(0.999999));
    vsout.lightProjZ = lightPos.z / lightPos.w;
    gl_Position      = lightPos;
}
