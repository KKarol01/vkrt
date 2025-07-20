#ifndef BINDLESS_STRUCTURES_INC_GLSL
#define BINDLESS_STRUCTURES_INC_GLSL

#define BINDLESS_STORAGE_BUFFER_BINDING 0
#define BINDLESS_STORAGE_IMAGE_BINDING 1
#define BINDLESS_COMBINED_IMAGE_BINDING 2
#define BINDLESS_SAMPLED_IMAGE_BINDING 3
#define BINDLESS_SAMPLER_BINDING 4
#define BINDLESS_ACCELERATION_STRUCT_BINDING 5
#define ENG_TYPE_INT int32_t
#define ENG_TYPE_UINT uint32_t
#define ENG_TYPE_UINT8 uint8_t
#define ENG_TYPE_FLOAT float

#ifndef __cplusplus
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#define ENG_TYPE_INT2 ivec2
#define ENG_TYPE_INT3 ivec3
#define ENG_TYPE_INT4 ivec4

#define ENG_TYPE_UINT2 uvec2
#define ENG_TYPE_UINT3 uvec3
#define ENG_TYPE_UINT4 uvec4

#define ENG_TYPE_FLOAT2 vec2
#define ENG_TYPE_FLOAT3 vec3
#define ENG_TYPE_FLOAT4 vec4

#define ENG_TYPE_MAT3 mat3
#define ENG_TYPE_MAT4 mat4

#define ENG_TYPE_UNSIZED(type, name) type name##_us[]

#define ENG_DECLARE_STORAGE_BUFFER(type)                                                                               \
    layout(set = 0, binding = BINDLESS_STORAGE_BUFFER_BINDING, scalar) buffer u_storageBuffers_##type
#define ENG_DECLARE_BINDLESS(type) storageBuffers_##type[]

#elif __cplusplus
#define ENG_TYPE_INT2 glm::ivec2
#define ENG_TYPE_INT3 glm::ivec3
#define ENG_TYPE_INT4 glm::ivec4

#define ENG_TYPE_UINT2 glm::uvec2
#define ENG_TYPE_UINT3 glm::uvec3
#define ENG_TYPE_UINT4 glm::uvec4

#define ENG_TYPE_FLOAT2 glm::vec2
#define ENG_TYPE_FLOAT3 glm::vec3
#define ENG_TYPE_FLOAT4 glm::vec4

#define ENG_TYPE_MAT3 glm::mat3
#define ENG_TYPE_MAT4 glm::mat4

#define ENG_TYPE_UNSIZED(type, name) type name##_us

#define ENG_DECLARE_STORAGE_BUFFER(type) struct type
#define ENG_DECLARE_BINDLESS(type)
#endif

struct GPUInstanceId
{
    uint32_t batch_id;  // index to indirect draw command for culling
    uint32_t transform; // index to transform
    uint32_t material;  // index to material
};

struct DrawIndirectCommand
{
    ENG_TYPE_UINT indexCount;
    ENG_TYPE_UINT instanceCount;
    ENG_TYPE_UINT firstIndex;
    ENG_TYPE_INT vertexOffset;
    ENG_TYPE_UINT firstInstance;
};
ENG_DECLARE_STORAGE_BUFFER(GPUDrawIndirectCommandsBuffer) {
    ENG_TYPE_UINT max_draw_count;
    ENG_TYPE_UINT padding0;
    ENG_TYPE_UNSIZED(DrawIndirectCommand, commands);
} ENG_DECLARE_BINDLESS(GPUDrawIndirectCommandsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUConstantsBuffer) {
    ENG_TYPE_MAT4 view;
    ENG_TYPE_MAT4 proj;
    ENG_TYPE_MAT4 proj_view;
    ENG_TYPE_MAT4 inv_view;
    ENG_TYPE_MAT4 inv_proj;
    ENG_TYPE_MAT4 inv_proj_view;
    ENG_TYPE_MAT3 rand_mat;
    ENG_TYPE_FLOAT3 cam_pos;
} ENG_DECLARE_BINDLESS(GPUConstantsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUIndicesBuffer) { 
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, indices); 
} ENG_DECLARE_BINDLESS(GPUIndicesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUVertexPositionsBuffer) { 
    ENG_TYPE_UNSIZED(ENG_TYPE_FLOAT3, positions); 
} ENG_DECLARE_BINDLESS(GPUVertexPositionsBuffer);

struct GPUVertexAttribute
{
    ENG_TYPE_FLOAT3 normal;
    ENG_TYPE_FLOAT2 uv;
    ENG_TYPE_FLOAT4 tangent;
};
ENG_DECLARE_STORAGE_BUFFER(GPUVertexAttributesBuffer) { 
    ENG_TYPE_UNSIZED(GPUVertexAttribute, attributes); 
} ENG_DECLARE_BINDLESS(GPUVertexAttributesBuffer);

/* unpacked mesh instance for gpu consumption */
struct GPUMeshInstance
{
    ENG_TYPE_UINT vertex_offset;
    ENG_TYPE_UINT index_offset;
    ENG_TYPE_UINT color_texture_idx;
    ENG_TYPE_UINT normal_texture_idx;
    ENG_TYPE_UINT metallic_roughness_idx;
};
ENG_DECLARE_STORAGE_BUFFER(GPUMeshInstancesBuffer) { 
    ENG_TYPE_UNSIZED(GPUMeshInstance, mesh_instances);
} ENG_DECLARE_BINDLESS(GPUMeshInstancesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUMeshletVerticesBuffer) { 
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, vertices);
} ENG_DECLARE_BINDLESS(GPUMeshletVerticesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUMeshletTrianglesBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, triangles); 
} ENG_DECLARE_BINDLESS(GPUMeshletTrianglesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUTransformsBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_MAT4, transforms);
} ENG_DECLARE_BINDLESS(GPUTransformsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUMeshletBoundingSpheresBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_FLOAT4, bounding_spheres); 
} ENG_DECLARE_BINDLESS(GPUMeshletBoundingSpheresBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUMeshletIdsBuffer) {
    ENG_TYPE_UINT count;
    ENG_TYPE_UINT padding0;
    ENG_TYPE_UNSIZED(GPUInstanceId, ids);
} ENG_DECLARE_BINDLESS(GPUMeshletIdsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUPostCullIdsBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, ids); 
} ENG_DECLARE_BINDLESS(GPUPostCullIdsBuffer);

#define VSM_PHYSICAL_PAGE_RESOLUTION 4096
#define VSM_VIRTUAL_PAGE_RESOLUTION 64
#define VSM_VIRTUAL_PAGE_PHYSICAL_SIZE (VSM_PHYSICAL_PAGE_RESOLUTION / VSM_VIRTUAL_PAGE_RESOLUTION)
#define VSM_NUM_CLIPMAPS 8
#define VSM_CLIP0_LENGTH 24.0
#define VSM_MAX_ALLOCS (VSM_VIRTUAL_PAGE_RESOLUTION * VSM_VIRTUAL_PAGE_RESOLUTION)
ENG_DECLARE_STORAGE_BUFFER(GPUVsmConstantsBuffer) {
    ENG_TYPE_MAT4 dir_light_view;
    ENG_TYPE_MAT4 dir_light_proj_view[VSM_NUM_CLIPMAPS];
    ENG_TYPE_FLOAT3 dir_light_dir;
    ENG_TYPE_UINT num_pages_xy;
    ENG_TYPE_UINT max_clipmap_index;
    ENG_TYPE_FLOAT texel_resolution; // todo: rename; reconsider
    ENG_TYPE_UINT num_frags;
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, pages);
} ENG_DECLARE_BINDLESS(GPUVsmConstantsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUVsmAllocConstantsBuffer) { 
    ENG_TYPE_UINT free_list_head;
} ENG_DECLARE_BINDLESS(GPUVsmAllocConstantsBuffer);

struct FFTOceanSettings
{
    ENG_TYPE_FLOAT num_samples;    // N
    ENG_TYPE_FLOAT patch_size;     // L
    ENG_TYPE_FLOAT2 wind_dir;      // w
    ENG_TYPE_FLOAT phillips_const; // A
    ENG_TYPE_FLOAT time_speed;     // A
    ENG_TYPE_FLOAT disp_lambda;
    ENG_TYPE_FLOAT small_l;
};

#ifndef __cplusplus

layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform usampler2D combinedImages_2du[];
layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform sampler2D combinedImages_2d[];
layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform sampler2DArray combinedImages_2dArray[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r16ui) restrict uniform uimage2D storageImages_2dr16ui[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r16f) restrict uniform image2D storageImages_2dr16f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r32ui) restrict uniform uimage2D storageImages_2dr32ui[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r32ui) restrict uniform uimage2DArray storageImages_2dr32uiArray[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba8) restrict uniform image2D storageImages_2drgba8[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba32f) restrict uniform image2D storageImages_2drgba32f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba16f) restrict uniform image2D storageImages_2drgba16f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, r32f) restrict uniform image2D storageImages_2dr32f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rg32f) restrict uniform image2D storageImages_2drg32f[];
layout(set = 0, binding = BINDLESS_STORAGE_IMAGE_BINDING, rgba8) restrict uniform image2DArray storageImages_2drgba8Array[];
layout(set = 0, binding = BINDLESS_SAMPLED_IMAGE_BINDING) uniform texture2D textures_2d[];
layout(set = 0, binding = BINDLESS_SAMPLER_BINDING) uniform sampler samplers[];

#define constants storageBuffers_GPUConstantsBuffer[constants_index]
#define vertex_pos_arr storageBuffers_GPUVertexPositionsBuffer[vertex_positions_index].positions_us
#define attrib_pos_arr storageBuffers_GPUVertexAttributesBuffer[vertex_attributes_index].attributes_us
#define transforms_arr storageBuffers_GPUTransformsBuffer[transforms_index].transforms_us
#define meshes_arr storageBuffers_GPUMeshInstancesBuffer[meshes_index].mesh_instances_us
#define meshlets_verts storageBuffers_GPUMeshletVerticesBuffer[meshlets_vertices_index].vertices_us
#define meshlets_tris storageBuffers_GPUMeshletTrianglesBuffer[meshlets_triangles_index].triangles_us

#endif

#endif