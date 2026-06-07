#ifndef ENG_COMMON_H
#define ENG_COMMON_H

#define ENG_TWO_PI  6.2831853071795864769252867    
#define ENG_PI      3.1415926535897932384626433
#define ENG_HALF_PI 1.5707963267948966192313216

#define ENG_BINDLESS_STORAGE_BUFFER_BINDING 0 
#define ENG_BINDLESS_STORAGE_IMAGE_BINDING 1
// #define BINDLESS_COMBINED_IMAGE_BINDING 2
#define ENG_BINDLESS_SAMPLED_IMAGE_BINDING 3
#define ENG_BINDLESS_SAMPLER_BINDING 4
#define ENG_BINDLESS_ACCELERATION_STRUCT_BINDING 5

#define ENG_SAMPLER_LINEAR 0
#define ENG_SAMPLER_LINEAR_CLAMP 1
#define ENG_SAMPLER_NEAREST 2 
#define ENG_SAMPLER_HIZ 3 
#define ENG_SAMPLER_COUNT 4

#define GPU_LIGHT_TYPE_POINT 0

#ifndef __cplusplus
#define ENG_INT int
#define ENG_INT2 int2
#define ENG_INT3 int3
#define ENG_INT4 int4 

#define ENG_UINT uint
#define ENG_UINT2 uint2
#define ENG_UINT3 uint3
#define ENG_UINT4 uint4

#define ENG_FLOAT float
#define ENG_FLOAT2 float2
#define ENG_FLOAT3 float3
#define ENG_FLOAT4 float4

#define ENG_MAT3x4 float3x4
#define ENG_MAT3 float3x3
#define ENG_MAT4 float4x4

#define ENG_INLINE

#elif __cplusplus
#define ENG_INT int32_t
#define ENG_INT2 glm::ivec2
#define ENG_INT3 glm::ivec3
#define ENG_INT4 glm::ivec4

#define ENG_UINT uint32_t
#define ENG_UINT2 glm::uvec2
#define ENG_UINT3 glm::uvec3
#define ENG_UINT4 glm::uvec4

#define ENG_FLOAT float
#define ENG_FLOAT2 glm::vec2
#define ENG_FLOAT3 glm::vec3
#define ENG_FLOAT4 glm::vec4

#define ENG_MAT3x4 glm::mat3x4
#define ENG_MAT3 glm::mat3
#define ENG_MAT4 glm::mat4

#define ENG_INLINE inline

#endif
 
#ifdef __cplusplus
namespace eng {
#endif

#ifndef NO_COMMON_STRUCTS
    // put other includes/types/bindless stuff here
    
struct GPUEngConstants
{
    ENG_UINT GPUVertexPositionBufferIndex;
    ENG_UINT GPUVertexAttributeBufferIndex;
    ENG_UINT GPUBoundingSpheresBufferIndex; // per-resource mesh bounding sphere buffer index
    ENG_UINT GPUTransformsBufferIndex; // per-instance transform buffer index
    ENG_UINT GPUMaterialBufferIndex; // per-instance material buffer index
    ENG_UINT GPULightsBufferIndex; // lights buffer index

    ENG_MAT4 view;
    ENG_MAT4 proj;
    ENG_MAT4 proj_view;
    ENG_MAT4 inv_view;
    ENG_MAT4 inv_proj;
    ENG_MAT4 inv_proj_view;
    ENG_MAT3 rand_mat;
    ENG_FLOAT3 cam_pos;

    ENG_UINT output_mode;
   
    ENG_UINT fwdp_enable;
    ENG_UINT fwdp_max_lights_per_tile;

    ENG_UINT mlt_frust_cull_enable;
    ENG_UINT mlt_occ_cull_enable;
	
	ENG_UINT frame_index;
};

struct GPUEngAOSettings
{
	ENG_FLOAT radius;
	ENG_FLOAT bias;
};

struct GPUVertexPosition
{
    ENG_FLOAT3 pos;
};

struct IndexedIndirectDrawCommand
{
    ENG_UINT indexCount;
    ENG_UINT instanceCount;
    ENG_UINT firstIndex;
    ENG_INT vertexOffset;
    ENG_UINT firstInstance;
};

struct GPUVertexAttribute
{
    ENG_FLOAT3 normal;
    ENG_FLOAT4 tangent;
    ENG_FLOAT2 uv;
};

struct GPUInstanceId
{
    ENG_UINT cmdi; // index of indirect draw command
    ENG_UINT resi; // index of resource shared by instances 
    ENG_UINT insti; // index of resource owned by instance
    ENG_UINT mati; // index of material
};

struct GPUMaterial
{
    ENG_UINT base_color_idx;
    ENG_UINT base_color_factor;
};

#endif

//
//  DEFINE CPU/GPU FUNS HERE
//
#ifndef ENG_NO_COMMON_FUNCS
ENG_INLINE ENG_UINT pack_unorm4x8(ENG_FLOAT r, ENG_FLOAT g, ENG_FLOAT b, ENG_FLOAT a)
{
    return (((ENG_UINT) (r * 255.0f + 0.5f)) << 0 | ((ENG_UINT) (g * 255.0f + 0.5f)) << 8 | ((ENG_UINT) (b * 255.0f + 0.5f)) << 16 | ((ENG_UINT) (a * 255.0f + 0.5f)) << 24);
}
ENG_INLINE ENG_FLOAT4 unpack_unorm4x8(ENG_UINT rgba)
{
    return ENG_FLOAT4((ENG_FLOAT) (rgba & 0xFF) / 255.0f, (ENG_FLOAT) ((rgba >> 8) & 0xFF) / 255.0f, (ENG_FLOAT) ((rgba >> 16) & 0xFF) / 255.0f, (ENG_FLOAT) ((rgba >> 24) & 0xFF) / 255.0f);
}
#endif

#ifdef __cplusplus
} // namespace eng
#endif

#ifndef __cplusplus
//
// DEFINE HLSL RESOURCE BINDINGS HERE
//
[[vk::binding(ENG_BINDLESS_STORAGE_BUFFER_BINDING, 0)]] RWByteAddressBuffer gRWBuffers[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float> gRWTextures2Dfloat[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float2> gRWTextures2Dfloat2[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float4> gRWTextures2Dfloat4[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D gTexture2Ds[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float> gTextures2Dfloat[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float2> gTextures2Dfloat2[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float4> gTextures2Dfloat4[];
[[vk::binding(ENG_BINDLESS_SAMPLER_BINDING, 0)]] SamplerState gSamplerStates[];
[[vk::binding(ENG_BINDLESS_ACCELERATION_STRUCT_BINDING, 0)]] RaytracingAccelerationStructure gTLASs[];

#define gSamplerLinear gSamplerStates[ENG_SAMPLER_LINEAR]
#define gSamplerLinearClamp gSamplerStates[ENG_SAMPLER_LINEAR_CLAMP]
#define gSamplerNearest gSamplerStates[ENG_SAMPLER_NEAREST]
#define gSamplerHiz gSamplerStates[ENG_SAMPLER_HIZ]

#define ENG_INVALID_HANDLE (~0u)
#define get_grwb(type, index) gRWBuffers[NonUniformResourceIndex(pc.type##BufferIndex)].Load<type>(index * sizeof(type))
#define get_grwbi(type, buffer_index, index) gRWBuffers[NonUniformResourceIndex(buffer_index)].Load<type>(index * sizeof(type))
#define get_grwb2(type, struct, index) gRWBuffers[NonUniformResourceIndex(struct.type##BufferIndex)].Load<type>(index * sizeof(type))

#define get_gt(type) gTextures2Dfloat4[NonUniformResourceIndex(pc.type##TextureIndex)]
#define get_gt2(type, format) gTextures2D##format[NonUniformResourceIndex(pc.type##TextureIndex)]
#define get_grwt2(type, format) gRWTextures2D##format[NonUniformResourceIndex(pc.type##RWTextureIndex)]

#endif

#if 0
struct IndexedIndirectDrawCommand
{
    ENG_UINT indexCount;
    ENG_UINT instanceCount;
    ENG_UINT firstIndex;
    ENG_INT vertexOffset;
    ENG_UINT firstInstance;
};
ENG_DECLARE_STORAGE_BUFFER(GPUDrawIndexedIndirectCommandsBuffer) {
    ENG_UNSIZED(IndexedIndirectDrawCommand, commands);
} ENG_DECLARE_BINDLESS(GPUDrawIndexedIndirectCommandsBuffer);
ENG_DECLARE_STORAGE_BUFFER(GPUIndirectCountsBuffer) {
    ENG_UNSIZED(ENG_UINT, counts);
} ENG_DECLARE_BINDLESS(GPUIndirectCountsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUIndexBuffer) { 
    ENG_UNSIZED(ENG_UINT, indices); 
} ENG_DECLARE_BINDLESS(GPUIndexBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUVertexPositionsBuffer) { 
    ENG_UNSIZED(ENG_FLOAT3, positions); 
} ENG_DECLARE_BINDLESS(GPUVertexPositionsBuffer);

struct GPUVertexAttribute
{
    ENG_FLOAT3 normal;
    ENG_FLOAT4 tangent;
    ENG_FLOAT2 uv;
};
ENG_DECLARE_STORAGE_BUFFER(GPUVertexAttributesBuffer) { 
    ENG_UNSIZED(GPUVertexAttribute, attributes); 
} ENG_DECLARE_BINDLESS(GPUVertexAttributesBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUTransformsBuffer) {
    ENG_UNSIZED(ENG_TYPE_MAT4, transforms);
} ENG_DECLARE_BINDLESS(GPUTransformsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUBoundingSpheresBuffer) {
    ENG_UNSIZED(ENG_FLOAT4, bounding_spheres); 
} ENG_DECLARE_BINDLESS(GPUBoundingSpheresBuffer);

struct GPUInstanceId
{
    uint32_t cmdi;      // index of indirect draw command
    uint32_t resi;      // index of resource shared by instances 
    uint32_t insti;     // index of resource owned by instance
    uint32_t mati;      // index of material
};
ENG_DECLARE_STORAGE_BUFFER(GPUInstanceIdsBuffer) {
    ENG_UINT count;
    ENG_UNSIZED(GPUInstanceId, ids);
} ENG_DECLARE_BINDLESS(GPUInstanceIdsBuffer);

struct GPUMaterial
{
    uint32_t base_color_idx;
};
ENG_DECLARE_STORAGE_BUFFER(GPUMaterialsBuffer) {
    ENG_UNSIZED(GPUMaterial, materials);
} ENG_DECLARE_BINDLESS(GPUMaterialsBuffer);

#define VSM_PHYSICAL_PAGE_RESOLUTION 4096
#define VSM_VIRTUAL_PAGE_RESOLUTION 64
#define VSM_VIRTUAL_PAGE_PHYSICAL_SIZE (VSM_PHYSICAL_PAGE_RESOLUTION / VSM_VIRTUAL_PAGE_RESOLUTION)
#define VSM_NUM_CLIPMAPS 8
#define VSM_CLIP0_LENGTH 24.0
#define VSM_MAX_ALLOCS (VSM_VIRTUAL_PAGE_RESOLUTION * VSM_VIRTUAL_PAGE_RESOLUTION)
ENG_DECLARE_STORAGE_BUFFER(GPUVsmConstantsBuffer) {
    ENG_MAT4 dir_light_view;
    ENG_MAT4 dir_light_proj_view[VSM_NUM_CLIPMAPS];
    ENG_FLOAT3 dir_light_dir;
    ENG_UINT num_pages_xy;
    ENG_UINT max_clipmap_index;
    ENG_FLOAT texel_resolution; // todo: rename; reconsider
    ENG_UINT num_frags;
    ENG_UNSIZED(ENG_UINT, pages);
} ENG_DECLARE_BINDLESS(GPUVsmConstantsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUVsmAllocConstantsBuffer) { 
    ENG_UINT free_list_head;
} ENG_DECLARE_BINDLESS(GPUVsmAllocConstantsBuffer);

struct FFTOceanSettings
{
    ENG_FLOAT num_samples;    // N
    ENG_FLOAT patch_size;     // L
    ENG_FLOAT2 wind_dir;      // w
    ENG_FLOAT phillips_const; // A
    ENG_FLOAT time_speed;     // A
    ENG_FLOAT disp_lambda;
    ENG_FLOAT small_l;
};

#define GPU_LIGHT_TYPE_POINT 0
struct GPULight
{
    ENG_FLOAT3 pos;
    ENG_FLOAT range;
    ENG_FLOAT4 color;
    ENG_FLOAT intensity;
    ENG_UINT type;
};
ENG_DECLARE_STORAGE_BUFFER(GPULightsBuffer) { 
    ENG_UINT count;
    ENG_UNSIZED(GPULight, lights);
} ENG_DECLARE_BINDLESS(GPULightsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUFWDPLightListsBuffer) { 
    ENG_UINT head;
    ENG_UNSIZED(ENG_UINT, lights);
} ENG_DECLARE_BINDLESS(GPUFWDPLightListsBuffer);

ENG_DECLARE_STORAGE_BUFFER(GPUFWDPLightGridsBuffer) { 
    ENG_UNSIZED(ENG_UINT2, grids);
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
