#version 460

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

layout(location = 0) in VsOut {
    vec3 position;
} vsout;

const float dk = 32.0;
const float md = 20.0;
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

}