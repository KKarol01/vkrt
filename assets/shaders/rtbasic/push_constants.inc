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

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer CombinedRTBuffers {
    PerTriangleMeshId mesh_ids;
    PerTlasTriangleOffsets offsets;
    MeshDatasBuffer meshes;
    PerTlasTransform transforms;
};

layout(scalar, buffer_reference, buffer_reference_align = 4) readonly buffer DDGIBuffer {
    ivec2 radiance_tex_size;
    ivec2 irradiance_tex_size;
    ivec2 visibility_tex_size;
    ivec2 probe_offset_tex_size;
	vec3 probe_start;
	uvec3 probe_counts;
	vec3 probe_walk;
    float min_probe_distance;
    float max_probe_distance;
	float min_dist; // min tracing dist
	float max_dist; // max tracing dist TODO: rename the actual vars
	float normal_bias;
    float max_probe_offset;
    uint frame_num;
	int irr_res;
	int vis_res;
    uint rays_per_probe;
    uint radiance_tex_idx;
    DDGI_DEBUG_PROBE_OFFSETS debug_probe_offsets;
};

layout(scalar, push_constant) uniform Constants {
    CombinedRTBuffers combined_rt_buffs;
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    DDGIBuffer ddgi;
    uint32_t mode;
};
