#include "normal_reconstruction.shared.hlsli"
#include "assets/shaders/util.hlsli"

ENG_PUSH_CONSTANTS(GPUNormalReconstructionParams)

static const uint LOCAL_SIZE = 8; 

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    RWTexture2D<float4> out_normal = pc.out_normal.get();
    Texture2D<float> depth_tex = pc.depth.get();
    const float4x4 proj = pc.engine.load().proj;
    
    uint2 dims;
    out_normal.GetDimensions(dims.x, dims.y);
    if (any(dtid.xy >= dims)) return;

    out_normal[dtid.xy] = float4(calculate_normal_from_depth(proj, int2(dtid.xy), int2(dims), depth_tex), 1.0);
}
