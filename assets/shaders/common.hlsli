#ifndef ENG_COMMON_HLSLI
#define ENG_COMMON_HLSLI

#include "assets/shaders/types.hlsli"
#include "assets/shaders/bindless.hlsli"

#define ENG_TWO_PI  6.2831853071795864769252867
#define ENG_PI      3.1415926535897932384626433
#define ENG_HALF_PI 1.5707963267948966192313216

#define GPU_LIGHT_TYPE_POINT 0

ENG_NAMESPACE_BEGIN

//
//  ENGINE-WIDE GPU STRUCTS (shared by C++ and HLSL)
//

struct GPUVertexPosition
{
    ENG_FLOAT3 pos;
};

struct GPUVertexAttribute
{
    ENG_FLOAT3 normal;
    ENG_FLOAT4 tangent;
    ENG_FLOAT2 uv;
};

struct GPUMaterial
{
    Texture2DRef<ENG_FLOAT4> base_color;
    ENG_UINT base_color_factor; // packed unorm4x8
};

struct GPUInstanceId
{
    ENG_UINT cmdi;  // index of indirect draw command
    ENG_UINT resi;  // index of resource shared by instances
    ENG_UINT insti; // index of resource owned by instance
    ENG_UINT mati;  // index of material
};

struct GPUIndexedIndirectDrawCommand
{
    ENG_UINT indexCount;
    ENG_UINT instanceCount;
    ENG_UINT firstIndex;
    ENG_INT vertexOffset;
    ENG_UINT firstInstance;
};

struct GPUEngConstants
{
    BufferRef<GPUVertexPosition> vertex_positions;
    BufferRef<GPUVertexAttribute> vertex_attributes;
    BufferRef<ENG_FLOAT4> bounding_spheres; // per-resource mesh bounding spheres
    BufferRef<ENG_MAT4> transforms;         // per-instance transforms
    BufferRef<GPUMaterial> materials;       // per-instance materials

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
};

struct GPUEngAOSettings
{
    ENG_FLOAT radius;
    ENG_FLOAT bias;
};

// Push constants every mesh-pass effect receives (pushed by pass::MeshPass).
struct GPUMeshPassParams
{
    BufferRef<GPUEngConstants> engine;
    BufferRef<GPUInstanceId> instances;
};

//
//  CPU/GPU FUNCTIONS
//

ENG_INLINE ENG_UINT pack_unorm4x8(ENG_FLOAT r, ENG_FLOAT g, ENG_FLOAT b, ENG_FLOAT a)
{
    return (((ENG_UINT)(r * 255.0f + 0.5f)) << 0 | ((ENG_UINT)(g * 255.0f + 0.5f)) << 8 |
            ((ENG_UINT)(b * 255.0f + 0.5f)) << 16 | ((ENG_UINT)(a * 255.0f + 0.5f)) << 24);
}
ENG_INLINE ENG_FLOAT4 unpack_unorm4x8(ENG_UINT rgba)
{
    return ENG_FLOAT4((ENG_FLOAT)(rgba & 0xFF) / 255.0f, (ENG_FLOAT)((rgba >> 8) & 0xFF) / 255.0f,
                      (ENG_FLOAT)((rgba >> 16) & 0xFF) / 255.0f, (ENG_FLOAT)((rgba >> 24) & 0xFF) / 255.0f);
}

ENG_NAMESPACE_END

#endif
