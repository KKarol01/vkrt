#version 460

#include "../bindless_structures.inc.glsl"

layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t vertex_attributes_index;
    uint32_t constants_index;
    uint32_t mesh_instances_index;
};

layout(location = 0) in VsOut {
    vec3 normal;
    vec3 tangent;
    vec2 uv;
    flat uint32_t instance_index;
} vsout;

layout(location = 0) out vec4 OUT_COLOR;

void main() {
	GPUMeshInstance mesh = GetResource(GPUMeshInstancesBuffer, mesh_instances_index).at[vsout.instance_index];
    vec4 sampled_color = texture(GetResource(CombinedImages2D, mesh.color_texture_idx), vsout.uv);
	OUT_COLOR = vec4(sampled_color.rgb, 1.0);
}