#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"
#include "vsm_common.inc.glsl"

layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t transform_buffer_index;
    uint32_t vsm_buffer_index;
};

layout(location = 0) out VsOut { vec3 position; }
vsout;

void main() {
    Vertex vertex = get_vertex_position(vertex_positions_index, gl_VertexIndex);
    vsout.position =
        vec3(GetResource(GPUTransformsBuffer, transform_buffer_index).at[gl_InstanceIndex] * vec4(vertex.position, 1.0));
    light_view = GetResource(VsmBuffer, vsm_buffer_index).dir_light_view;
    vsm_rclip_0_mat = GetResource(VsmBuffer, vsm_buffer_index).dir_light_proj * light_view;
    vec4 proj_pos = vsm_rclip_0_mat * vec4(vsout.position, 1.0);
    gl_Position = vec4(vsm_calc_virtual_coords(vsout.position), proj_pos.z, 1.0);

#if 0
    vsm_rclip_0_mat = proj_view * light_view;
    vec3 lspos = vsm_calc_rclip(vsout.position, 0);
    vec2 clip_index2 = vec2(
        ceil(log2(max(abs(lspos.x), 1.0))),
        ceil(log2(max(abs(lspos.y), 1.0)))
    );
    int clip_index = 0; int(max(clip_index2.x, clip_index2.y));
    lspos = vsm_calc_sclip(vsout.position, clip_index);
    lspos.xy = lspos.xy;
    //lspos.xy = fract(lspos.xy);
    vec2 vtc = lspos.xy;
    //ivec2 pti = ivec2(floor(vtc * vec2(vsmconsts.num_pages_xy)));
    gl_Position = vec4(vtc, lspos.z, 1.0);
#endif
}