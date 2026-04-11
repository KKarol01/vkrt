#ifndef ENG_COMMON_H
#define ENG_COMMON_H

#define ENG_BINDLESS_STORAGE_BUFFER_BINDING 0
#define ENG_BINDLESS_STORAGE_IMAGE_BINDING 1
// #define BINDLESS_COMBINED_IMAGE_BINDING 2
#define ENG_BINDLESS_SAMPLED_IMAGE_BINDING 3
#define ENG_BINDLESS_SAMPLER_BINDING 4
#define ENG_BINDLESS_ACCELERATION_STRUCT_BINDING 5

#define ENG_SAMPLER_LINEAR 0
#define ENG_SAMPLER_NEAREST 1
#define ENG_SAMPLER_HIZ 2

#define GPU_LIGHT_TYPE_POINT 0

#ifndef __cplusplus
#define ENG_TYPE_INT int
#define ENG_TYPE_INT2 int2
#define ENG_TYPE_INT3 int3
#define ENG_TYPE_INT4 int4

#define ENG_TYPE_UINT uint
#define ENG_TYPE_UINT2 uint2
#define ENG_TYPE_UINT3 uint3
#define ENG_TYPE_UINT4 uint4

#define ENG_TYPE_FLOAT float
#define ENG_TYPE_FLOAT2 float2
#define ENG_TYPE_FLOAT3 float3
#define ENG_TYPE_FLOAT4 float4

#define ENG_TYPE_MAT3x4 float3x4
#define ENG_TYPE_MAT3 float3x3
#define ENG_TYPE_MAT4 float4x4

#define ENG_DECLARE_STORAGE_BUFFER(type) struct type

#elif __cplusplus
#define ENG_TYPE_INT int32_t
#define ENG_TYPE_INT2 glm::ivec2
#define ENG_TYPE_INT3 glm::ivec3
#define ENG_TYPE_INT4 glm::ivec4

#define ENG_TYPE_UINT uint32_t
#define ENG_TYPE_UINT2 glm::uvec2
#define ENG_TYPE_UINT3 glm::uvec3
#define ENG_TYPE_UINT4 glm::uvec4

#define ENG_TYPE_FLOAT float
#define ENG_TYPE_FLOAT2 glm::vec2
#define ENG_TYPE_FLOAT3 glm::vec3
#define ENG_TYPE_FLOAT4 glm::vec4

#define ENG_TYPE_MAT3x4 glm::mat3x4
#define ENG_TYPE_MAT3 glm::mat3
#define ENG_TYPE_MAT4 glm::mat4

#define ENG_DECLARE_STORAGE_BUFFER(type) struct type

#endif

#ifndef NO_COMMON_STRUCTS
    // put other includes/types/bindless stuff here
    
struct GPUEngConstants
{
    ENG_TYPE_UINT vposb; // vertex positions buffer index
    ENG_TYPE_UINT vatrb; // vertex attributes buffer index
    ENG_TYPE_UINT vidxb; // index buffer index
    ENG_TYPE_UINT GPUBoundingSpheresBufferIndex; // per-resource mesh bounding sphere buffer index
    ENG_TYPE_UINT GPUTransformsBufferIndex; // per-instance transform buffer index
    ENG_TYPE_UINT rmatb; // per-instance material buffer index
    ENG_TYPE_UINT GPULightsBufferIndex; // lights buffer index

    ENG_TYPE_MAT4 view;
    ENG_TYPE_MAT4 prev_view;
    ENG_TYPE_MAT4 proj;
    ENG_TYPE_MAT4 proj_view;
    ENG_TYPE_MAT4 inv_view;
    ENG_TYPE_MAT4 inv_proj;
    ENG_TYPE_MAT4 inv_proj_view;
    ENG_TYPE_MAT3 rand_mat;
    ENG_TYPE_FLOAT3 cam_pos;

    ENG_TYPE_UINT output_mode;
   
    ENG_TYPE_UINT fwdp_enable;
    ENG_TYPE_UINT fwdp_max_lights_per_tile;

    ENG_TYPE_UINT mlt_frust_cull_enable;
    ENG_TYPE_UINT mlt_occ_cull_enable;

};

struct GPUEngAOSettings
{
	ENG_TYPE_FLOAT radius;
	ENG_TYPE_FLOAT bias;
};

struct GPUVertexPosition
{
    ENG_TYPE_FLOAT3 pos;
};

struct IndexedIndirectDrawCommand
{
    ENG_TYPE_UINT indexCount;
    ENG_TYPE_UINT instanceCount;
    ENG_TYPE_UINT firstIndex;
    ENG_TYPE_INT vertexOffset;
    ENG_TYPE_UINT firstInstance;
};

struct GPUVertexAttribute
{
    ENG_TYPE_FLOAT3 normal;
    ENG_TYPE_FLOAT4 tangent;
    ENG_TYPE_FLOAT2 uv;
};

struct GPUInstanceId
{
    ENG_TYPE_UINT cmdi; // index of indirect draw command
    ENG_TYPE_UINT resi; // index of resource shared by instances 
    ENG_TYPE_UINT insti; // index of resource owned by instance
    ENG_TYPE_UINT mati; // index of material
};

struct GPUMaterial
{
    ENG_TYPE_UINT base_color_idx;
};

#endif

#ifndef __cplusplus

#define ENG_PI      3.1415926535897932384626433
#define ENG_HALF_PI 1.5707963267948966192313216

[[vk::binding(ENG_BINDLESS_STORAGE_BUFFER_BINDING, 0)]] RWByteAddressBuffer gRWBuffers[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float4> gRWTexture2Df4s[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float> gRWTexture2Df1s[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D gTexture2Ds[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float> gTexture2Df1s[];
[[vk::binding(ENG_BINDLESS_SAMPLER_BINDING, 0)]] SamplerState gSamplerStates[];
[[vk::binding(ENG_BINDLESS_ACCELERATION_STRUCT_BINDING, 0)]] RaytracingAccelerationStructure gTLASs[];


#define get_gsb(type, index) gRWBuffers[pc.type##BufferIndex].Load<type>(index * sizeof(type))

#endif

#if 0
struct IndexedIndirectDrawCommand
{
    ENG_TYPE_UINT indexCount;
    ENG_TYPE_UINT instanceCount;
    ENG_TYPE_UINT firstIndex;
    ENG_TYPE_INT vertexOffset;
    ENG_TYPE_UINT firstInstance;
};
ENG_DECLARE_STORAGE_BUFFER(GPUDrawIndexedIndirectCommandsBuffer) {
    ENG_TYPE_UNSIZED(IndexedIndirectDrawCommand, commands);
} ENG_DECLARE_BINDLESS(GPUDrawIndexedIndirectCommandsBuffer);
ENG_DECLARE_STORAGE_BUFFER(GPUIndirectCountsBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, counts);
} ENG_DECLARE_BINDLESS(GPUIndirectCountsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUIndicesBuffer) { 
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, indices); 
} ENG_DECLARE_BINDLESS(GPUIndicesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUVertexPositionsBuffer) { 
    ENG_TYPE_UNSIZED(ENG_TYPE_FLOAT3, positions); 
} ENG_DECLARE_BINDLESS(GPUVertexPositionsBuffer);

struct GPUVertexAttribute
{
    ENG_TYPE_FLOAT3 normal;
    ENG_TYPE_FLOAT4 tangent;
    ENG_TYPE_FLOAT2 uv;
};
ENG_DECLARE_STORAGE_BUFFER(GPUVertexAttributesBuffer) { 
    ENG_TYPE_UNSIZED(GPUVertexAttribute, attributes); 
} ENG_DECLARE_BINDLESS(GPUVertexAttributesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUTransformsBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_MAT4, transforms);
} ENG_DECLARE_BINDLESS(GPUTransformsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUBoundingSpheresBuffer) {
    ENG_TYPE_UNSIZED(ENG_TYPE_FLOAT4, bounding_spheres); 
} ENG_DECLARE_BINDLESS(GPUBoundingSpheresBuffer);

struct GPUInstanceId
{
    uint32_t cmdi;      // index of indirect draw command
    uint32_t resi;      // index of resource shared by instances 
    uint32_t insti;     // index of resource owned by instance
    uint32_t mati;      // index of material
};
ENG_DECLARE_STORAGE_BUFFER(GPUInstanceIdsBuffer) {
    ENG_TYPE_UINT count;
    ENG_TYPE_UNSIZED(GPUInstanceId, ids);
} ENG_DECLARE_BINDLESS(GPUInstanceIdsBuffer);

struct GPUMaterial
{
    uint32_t base_color_idx;
};
ENG_DECLARE_STORAGE_BUFFER(GPUMaterialsBuffer) {
    ENG_TYPE_UNSIZED(GPUMaterial, materials);
} ENG_DECLARE_BINDLESS(GPUMaterialsBuffer);

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

#define GPU_LIGHT_TYPE_POINT 0
struct GPULight
{
    ENG_TYPE_FLOAT3 pos;
    ENG_TYPE_FLOAT range;
    ENG_TYPE_FLOAT4 color;
    ENG_TYPE_FLOAT intensity;
    ENG_TYPE_UINT type;
};
ENG_DECLARE_STORAGE_BUFFER(GPULightsBuffer) { 
    ENG_TYPE_UINT count;
    ENG_TYPE_UNSIZED(GPULight, lights);
} ENG_DECLARE_BINDLESS(GPULightsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUFWDPLightListsBuffer) { 
    ENG_TYPE_UINT head;
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT, lights);
} ENG_DECLARE_BINDLESS(GPUFWDPLightListsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUFWDPLightGridsBuffer) { 
    ENG_TYPE_UNSIZED(ENG_TYPE_UINT2, grids);
} ENG_DECLARE_BINDLESS(GPUFWDPLightGridsBuffer);

#ifndef __cplusplus

layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, r16ui) restrict uniform uimage2D gsi_2dr16ui[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, r16f) restrict uniform image2D gsi_2dr16f[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, r32ui) restrict uniform uimage2D gsi_2dr32ui[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, r32ui) restrict uniform uimage2DArray gsi_2dr32uiv[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, rgba8) restrict uniform image2D gsi_2drgba8[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, rgba32f) restrict uniform image2D gsi_2drgba32f[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, rgba16f) restrict uniform image2D gsi_2drgba16f[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, r32f) restrict uniform image2D gsi_2dr32f[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, rg32f) restrict uniform image2D gsi_2drg32f[];
layout(set = 0, binding = ENG_BINDLESS_STORAGE_IMAGE_BINDING, rgba8) restrict uniform image2DArray gsi_2drgba8v[];
layout(set = 0, binding = ENG_BINDLESS_SAMPLED_IMAGE_BINDING) uniform texture2D gt_2d[];
layout(set = 0, binding = ENG_BINDLESS_SAMPLER_BINDING) uniform sampler g_samplers[];

#endif

#endif

#endif