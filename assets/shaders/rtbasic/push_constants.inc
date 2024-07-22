struct Vertex {
    vec3 pos;
    vec3 nor;
    vec2 uv;
};

struct MeshData {
    //uint32_t index_offset;
    //uint32_t vertex_offset;
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
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer CombinedRTBuffers {
    PerTriangleMeshId mesh_ids;
    PerTlasTriangleOffsets offsets;
    MeshDatasBuffer meshes;
};

layout(scalar, buffer_reference, buffer_reference_align = 4) readonly buffer DDGIBuffer {
	vec3 probe_start;
	uvec3 probe_counts;
	vec3 probe_walk;
	float min_dist;
	float max_dist;
	float normal_bias;
	uint irr_res;
    uint rays_per_probe;
    uint irr_tex_idx;
};

layout(scalar, push_constant) uniform Constants {
    CombinedRTBuffers combined_rt_buffs;
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    DDGIBuffer ddgi;
    uint32_t mode;
};
