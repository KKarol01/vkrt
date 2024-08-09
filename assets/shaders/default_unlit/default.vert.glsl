#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nor;
layout(location = 2) in vec2 uv;

layout(location = 0) flat out uint vmesh_id;
layout(location = 1) out vec2 vuv;

struct Vertex {
    vec3 pos;
    vec3 nor;
    vec2 uv;
};

struct MeshData {
    //uint32_t vertex_offset; // supposed to be 0 always (for now)
    uint32_t index_offset;
    uint32_t index_count;
    uint32_t color_texture;
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer VertexBuffer {
    Vertex vertices[]; 
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer IndexBuffer {
    uint32_t indices[]; 
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerTriangleMeshId {
    uint32_t ids[];
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer MeshDatasBuffer {
    MeshData mesh_datas[];
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerTlasTriangleOffsets {
    uint32_t offsets[];
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerTlasTransform {
    mat4x3 transforms[];
};
layout(scalar, buffer_reference, buffer_reference_align = 8) buffer DDGI_DEBUG_PROBE_OFFSETS {
    vec3 probe_offsets[];
};
layout(scalar, buffer_reference, buffer_reference_align = 8) buffer PerMeshInstanceTransform {
    mat4x3 transforms[];
};
layout(scalar, buffer_reference, buffer_reference_align = 8) buffer PerMeshInstanceMeshId {
    uint32_t ids[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer CombinedRTBuffers {
    PerTriangleMeshId mesh_ids;
    PerTlasTriangleOffsets offsets;
    MeshDatasBuffer meshes;
    PerTlasTransform transforms;
};

layout(scalar, push_constant) uniform Constants {
    CombinedRTBuffers combined_rt_buffs;
    PerMeshInstanceTransform transforms;
    PerMeshInstanceMeshId ids;
};

layout(binding = 14, set = 0) uniform CameraProperties {
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation;
} cam;

void main() {
    uint triangle_id = (gl_VertexIndex + 3 - gl_VertexIndex % 3) / 2;
    uint mesh_id = ids.ids[gl_InstanceIndex];
    vmesh_id = mesh_id;
    vuv = uv;

	gl_Position = inverse(cam.projInverse) * inverse(cam.viewInverse) * vec4(transforms.transforms[gl_InstanceIndex] * vec4(pos, 1.0), 1.0);
}