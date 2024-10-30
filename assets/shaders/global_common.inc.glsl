struct Vertex {
    vec3 pos;
    vec3 nor;
    vec2 uv;
};

struct MeshData {
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t color_texture;
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer GlobalBuffer {
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation; // halton 4-frame jitter
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer VertexBuffer {
    Vertex at[]; 
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer IndexBuffer {
    uint32_t at[]; 
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer MeshDataBuffer {
    MeshData at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerMeshInstanceMeshIdBuffer {
    uint32_t at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerMeshInstanceTransformBuffer {
    mat4 at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerTlasInstanceBlasGeometryOffsetBuffer {
    uint32_t at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerBlasGeometryTriangleOffsetBuffer {
    uint32_t at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerTriangleMeshIdBuffer {
    uint32_t at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) writeonly buffer DebugDDGIProbeOffsetsBuffer {
    vec3 at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer DDGIBuffer {
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
    DebugDDGIProbeOffsetsBuffer debug_probe_offsets;
};