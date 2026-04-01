#include "./common.hlsli"
#include "./assets/shaders/util.hlsli"

static const uint LOCAL_SIZE = 8;
static const uint TILE_BORDER = 1;
static const uint TILE_SIZE = LOCAL_SIZE * LOCAL_SIZE + TILE_BORDER * 2;

groupshared float tile_xy[TILE_SIZE * TILE_SIZE];
groupshared float tile_z[TILE_SIZE * TILE_SIZE];

float3 depth_to_view_pos(uint2 dtid, uint2 dims, float depth)
{
    float2 uv = (float2(dtid) + 0.5) / float2(dims);
    float2 ndc_xy = uv * 2.0 - 1.0;
	return unproject_inf_revz_depth(float3(ndc_xy.xy, depth)); 
}
[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    RWTexture2D<float4> normalTex = gRWTexture2Df4s[pc.NormalImageIndex];
    Texture2D<float4> depthTex = gTexture2Ds[pc.DepthImageIndex];
    
    uint2 dims;
    normalTex.GetDimensions(dims.x, dims.y);
    if (any(dtid.xy >= dims)) return;

    // Load center and 4 neighbors
    float3 p0 = depth_to_view_pos(dtid.xy, dims, depthTex.Load(int3(dtid.xy, 0)).x);
    float3 pR = depth_to_view_pos(dtid.xy + int2(1, 0),  dims, depthTex.Load(int3(dtid.xy + int2(1, 0), 0)).x);
    float3 pL = depth_to_view_pos(dtid.xy + int2(-1, 0), dims, depthTex.Load(int3(dtid.xy + int2(-1, 0), 0)).x);
    float3 pD = depth_to_view_pos(dtid.xy + int2(0, 1),  dims, depthTex.Load(int3(dtid.xy + int2(0, 1), 0)).x);
    float3 pU = depth_to_view_pos(dtid.xy + int2(0, -1), dims, depthTex.Load(int3(dtid.xy + int2(0, -1), 0)).x);

    // Pick best horizontal and vertical vectors based on smallest depth delta
    float3 vH = (abs(pR.z - p0.z) < abs(pL.z - p0.z)) ? (pR - p0) : (p0 - pL);
    float3 vV = (abs(pD.z - p0.z) < abs(pU.z - p0.z)) ? (pD - p0) : (p0 - pU);

    // Right-Handed (+X Right, +Y Down): Normal = Horizontal x Vertical
    // This results in +Z pointing toward the camera.
    float3 n = normalize(cross(vV, vH));

    normalTex[dtid.xy] = float4(n, 1.0);
}