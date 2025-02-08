#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "../bindless_structures.inc.glsl"

layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t vertex_attributes_index;
    uint32_t constants_index;
    uint32_t mesh_instances_index;
    uint32_t transform_buffer_index;
    uint32_t vsm_buffer_index;
};

layout(location = 0) out VsOut {
    vec3 position;
} vsout;

const float dk = 32.0;
const float md = 200.0;
const mat4 proj_view = mat4(
    2.0 / dk, 0.0, 0.0, 0.0,
    0.0, 2.0 / dk, 0.0, 0.0,
    0.0, 0.0, 1.0 / md, 0.0,
    0.0, 0.0, 0.0, 1.0
);

vec3 cam_pos = vec3(0.0, 0.0, 0.0);
mat4 light_view = mat4(1.0);
mat4 vsm_rclip_0_mat = mat4(1.0);

vec3 vsm_clip0_to_clip_n(vec3 o, int clip_index) {
    return vec3(o.xy * vec2(1.0 / float(1 << clip_index)), o.z);
}

vec3 vsm_calc_rclip(vec3 world_pos, int clip_index) {
    return vec3(vsm_clip0_to_clip_n(vec3(vsm_rclip_0_mat * vec4(world_pos, 1.0)), clip_index));
}

vec3 vsm_calc_sclip(vec3 world_pos, int clip_index) {
    vec3 res = vsm_calc_rclip(world_pos, clip_index);
    return res - vsm_clip0_to_clip_n(vsm_rclip_0_mat[3].xyz, clip_index);
}

void main() {
    Vertex vertex = get_vertex(vertex_positions_index, vertex_attributes_index, gl_VertexIndex);
    vsout.position = vec3(GetResource(GPUTransformsBuffer, transform_buffer_index).at[gl_InstanceIndex] * vec4(vertex.position, 1.0));
    //const vec3 cam_pos = vec3(GetResource(GPUConstantsBuffer, constants_index).constants.inv_view * vec4(0.0, 0.0, 0.0, 1.0));
    light_view = GetResource(VsmBuffer, vsm_buffer_index).dir_light_view;
    
    gl_Position = GetResource(VsmBuffer, vsm_buffer_index).dir_light_proj * light_view * vec4(vsout.position, 1.0);
    gl_Position.z = 0.6;
    
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