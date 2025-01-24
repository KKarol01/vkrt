#define BINDLESS_STORAGE_BUFFER_BINDING 0
#define BINDLESS_STORAGE_IMAGE_BINDING 1
#define BINDLESS_COMBINED_IMAGE_BINDING 2
#define ENG_TYPE_INT int32_t
#define ENG_TYPE_UINT uint32_t
#define ENG_TYPE_FLOAT float


#ifdef VULKAN

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#define ENG_TYPE_IVEC2 ivec2
#define ENG_TYPE_IVEC3 ivec3
#define ENG_TYPE_IVEC4 ivec4

#define ENG_TYPE_UVEC2 uvec2
#define ENG_TYPE_UVEC3 uvec3
#define ENG_TYPE_UVEC4 uvec4

#define ENG_TYPE_VEC2 vec2
#define ENG_TYPE_VEC3 vec3
#define ENG_TYPE_VEC4 vec4

#define ENG_TYPE_MAT3 mat3
#define ENG_TYPE_MAT4 mat4

#define BINDLESS_GET_LAYOUT_NAME(Name) Name##BindlessArray

#define BINDLESS_DECLARE_STORAGE_BUFFER(access, name, data) \
	layout(scalar, set=0, binding=BINDLESS_STORAGE_BUFFER_BINDING) access buffer name data BINDLESS_GET_LAYOUT_NAME(name)[]

#define BINDLESS_DECLARE_IMAGE(access, name, format, dim) \
	layout(set=0, binding=BINDLESS_STORAGE_IMAGE_BINDING, format) uniform image##dim BINDLESS_GET_LAYOUT_NAME(name)[]

#define BINDLESS_DECLARE_COMBINED_IMAGE(dim, name) \
	layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform sampler##dim BINDLESS_GET_LAYOUT_NAME(name)[]

#define GetResource(name, index) BINDLESS_GET_LAYOUT_NAME(name)[nonuniformEXT(index)]

#elif __cplusplus
#define ENG_TYPE_IVEC2 glm::ivec2
#define ENG_TYPE_IVEC3 glm::ivec3
#define ENG_TYPE_IVEC4 glm::ivec4

#define ENG_TYPE_UVEC2 glm::uvec2
#define ENG_TYPE_UVEC3 glm::uvec3
#define ENG_TYPE_UVEC4 glm::uvec4

#define ENG_TYPE_VEC2 glm::vec2
#define ENG_TYPE_VEC3 glm::vec3
#define ENG_TYPE_VEC4 glm::vec4

#define ENG_TYPE_MAT3 glm::mat3
#define ENG_TYPE_MAT4 glm::mat4
#endif

struct GPUConstants {
	ENG_TYPE_MAT4 view;
	ENG_TYPE_MAT4 proj;
	ENG_TYPE_MAT4 inv_view;
	ENG_TYPE_MAT4 inv_proj;
	ENG_TYPE_MAT3 rand_mat;
};

/* unpacked mesh instance for gpu consumption */
struct GPUMeshInstance {
    ENG_TYPE_UINT vertex_offset;
    ENG_TYPE_UINT index_offset;
    ENG_TYPE_UINT color_texture_idx;
    ENG_TYPE_UINT normal_texture_idx;
    ENG_TYPE_UINT metallic_roughness_idx;
};

struct GPUVertexAttribute {
	ENG_TYPE_VEC3 normal;
	ENG_TYPE_VEC2 uv;
	ENG_TYPE_VEC4 tangent;
};

#ifdef VULKAN
BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUIndicesBuffer, {
	uint32_t at[];
});

BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUConstantsBuffer, {
	GPUConstants constants;
});

BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUVertexPositionsBuffer, {
	ENG_TYPE_VEC3 at[];
});

BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUVertexAttributesBuffer, {
	GPUVertexAttribute at[];
});

BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUMeshInstancesBuffer, {
	GPUMeshInstance at[];
});

BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUTransformsBuffer, {
	ENG_TYPE_MAT4 at[];
});

BINDLESS_DECLARE_IMAGE(, StorageImages2Drgba8, rgba8, 2D);
BINDLESS_DECLARE_COMBINED_IMAGE(2D, CombinedImages2D);

// ---

vec3 get_vertex_position(uint32_t buffer_index, uint32_t vertex_index) {
	return GetResource(GPUVertexPositionsBuffer, buffer_index).at[vertex_index];
}

struct Vertex {
	vec3 position;
	vec3 normal;
	vec2 uv;
	vec3 tangent;
};
Vertex get_vertex(uint32_t position_index, uint32_t attributes_index, uint32_t vertex_index) {
	Vertex vx;
	vx.position = GetResource(GPUVertexPositionsBuffer, position_index).at[vertex_index];
	vx.normal = GetResource(GPUVertexAttributesBuffer, attributes_index).at[vertex_index].normal;
	vx.uv = GetResource(GPUVertexAttributesBuffer, attributes_index).at[vertex_index].uv;
	vx.tangent = GetResource(GPUVertexAttributesBuffer, attributes_index).at[vertex_index].tangent.xyz;
	vx.tangent *= GetResource(GPUVertexAttributesBuffer, attributes_index).at[vertex_index].tangent.w;
	return vx;
}



#endif