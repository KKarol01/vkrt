#ifndef BINDLESS_STRUCTURES_INC_GLSL
#define BINDLESS_STRUCTURES_INC_GLSL

#define BINDLESS_STORAGE_BUFFER_BINDING 0
#define BINDLESS_STORAGE_IMAGE_BINDING 1
#define BINDLESS_COMBINED_IMAGE_BINDING 2
#define BINDLESS_ACCELERATION_STRUCT_BINDING 3
#define ENG_TYPE_INT int32_t
#define ENG_TYPE_UINT uint32_t
#define ENG_TYPE_FLOAT float

#ifndef __cplusplus
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

#define ENG_TYPE_UNSIZED(type, name) type name##_us[]

#define ENG_DECLARE_STORAGE_BUFFERS(type) layout(set = 0, binding = BINDLESS_STORAGE_BUFFER_BINDING, scalar) buffer u_storageBuffers_##type
#define ENG_DECLARE_BINDLESS(type) storageBuffers_##type[]

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

#define ENG_TYPE_UNSIZED(type, name) type name##_us

#define ENG_DECLARE_STORAGE_BUFFERS(type) struct type
#define ENG_DECLARE_BINDLESS(type)
#endif

// todo: make those readonly

ENG_DECLARE_STORAGE_BUFFERS(GPUConstantsBuffer) {
	ENG_TYPE_MAT4 view;
	ENG_TYPE_MAT4 proj;
	ENG_TYPE_MAT4 proj_view;
	ENG_TYPE_MAT4 inv_view;
	ENG_TYPE_MAT4 inv_proj;
	ENG_TYPE_MAT4 inv_proj_view;
	ENG_TYPE_MAT3 rand_mat;
	ENG_TYPE_VEC3 cam_pos;
} ENG_DECLARE_BINDLESS(GPUConstantsBuffer);

ENG_DECLARE_STORAGE_BUFFERS(GPUIndicesBuffer) {
	ENG_TYPE_UNSIZED(ENG_TYPE_UINT, indices);
} ENG_DECLARE_BINDLESS(GPUIndicesBuffer);

ENG_DECLARE_STORAGE_BUFFERS(GPUVertexPositionsBuffer) {
	ENG_TYPE_UNSIZED(ENG_TYPE_VEC3, positions);
} ENG_DECLARE_BINDLESS(GPUVertexPositionsBuffer);

struct GPUVertexAttribute {
	ENG_TYPE_VEC3 normal;
	ENG_TYPE_VEC2 uv;
	ENG_TYPE_VEC4 tangent;
};
ENG_DECLARE_STORAGE_BUFFERS(GPUVertexAttributesBuffer) {
	ENG_TYPE_UNSIZED(GPUVertexAttribute, attributes);
} ENG_DECLARE_BINDLESS(GPUVertexAttributesBuffer);

/* unpacked mesh instance for gpu consumption */
struct GPUMeshInstance {
    ENG_TYPE_UINT vertex_offset;
    ENG_TYPE_UINT index_offset;
    ENG_TYPE_UINT color_texture_idx;
    ENG_TYPE_UINT normal_texture_idx;
    ENG_TYPE_UINT metallic_roughness_idx;
};
ENG_DECLARE_STORAGE_BUFFERS(GPUMeshInstancesBuffer) {
	ENG_TYPE_UNSIZED(GPUMeshInstance, mesh_instances);
} ENG_DECLARE_BINDLESS(GPUMeshInstancesBuffer);

ENG_DECLARE_STORAGE_BUFFERS(GPUTransformsBuffer) {
	ENG_TYPE_UNSIZED(ENG_TYPE_MAT4, transforms);
} ENG_DECLARE_BINDLESS(GPUTransformsBuffer);

#define VSM_PHYSICAL_PAGE_RESOLUTION 4096
#define VSM_VIRTUAL_PAGE_RESOLUTION 64
#define VSM_VIRTUAL_PAGE_PHYSICAL_SIZE (VSM_PHYSICAL_PAGE_RESOLUTION / VSM_VIRTUAL_PAGE_RESOLUTION)
#define VSM_NUM_CLIPMAPS 8
#define VSM_CLIP0_LENGTH 24.0
#define VSM_MAX_ALLOCS (VSM_VIRTUAL_PAGE_RESOLUTION * VSM_VIRTUAL_PAGE_RESOLUTION)
ENG_DECLARE_STORAGE_BUFFERS(GPUVsmConstantsBuffer) {
	ENG_TYPE_MAT4 dir_light_view;
	ENG_TYPE_MAT4 dir_light_proj_view[VSM_NUM_CLIPMAPS];
	ENG_TYPE_VEC3 dir_light_dir;
	ENG_TYPE_UINT num_pages_xy;
	ENG_TYPE_UINT max_clipmap_index;
	ENG_TYPE_FLOAT texel_resolution; // todo: rename; reconsider
	ENG_TYPE_UINT num_frags;
	ENG_TYPE_UNSIZED(ENG_TYPE_UINT, pages);
} ENG_DECLARE_BINDLESS(GPUVsmConstantsBuffer);

ENG_DECLARE_STORAGE_BUFFERS(GPUVsmAllocConstantsBuffer) {
	ENG_TYPE_UINT free_list_head;
} ENG_DECLARE_BINDLESS(GPUVsmAllocConstantsBuffer);

struct FFTOceanSettings {
    ENG_TYPE_FLOAT num_samples;    // N
    ENG_TYPE_FLOAT patch_size;     // L
    ENG_TYPE_VEC2 wind_dir;        // w
    ENG_TYPE_FLOAT phillips_const; // A
};

#ifndef __cplusplus

layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform sampler2D combinedImages_2d[];
layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform sampler2DArray combinedImages_2dArray[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r32ui) restrict uniform uimage2D storageImages_2dr32ui[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r32ui) restrict uniform uimage2DArray storageImages_2dr32uiArray[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba8) restrict uniform image2D storageImages_2drgba8[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba32f) restrict uniform image2D storageImages_2drgba32f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba16f) restrict uniform image2D storageImages_2drgba16f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rg32f) restrict uniform image2D storageImages_2drg32f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba8) restrict uniform image2DArray storageImages_2drgba8Array[];

#define constants		storageBuffers_GPUConstantsBuffer[constants_index]
#define vertex_pos_arr	storageBuffers_GPUVertexPositionsBuffer[vertex_positions_index].positions_us
#define attrib_pos_arr	storageBuffers_GPUVertexAttributesBuffer[vertex_attributes_index].attributes_us
#define transforms_arr	storageBuffers_GPUTransformsBuffer[transforms_index].transforms_us
#define meshes_arr		storageBuffers_GPUMeshInstancesBuffer[meshes_index].mesh_instances_us

#endif

#endif