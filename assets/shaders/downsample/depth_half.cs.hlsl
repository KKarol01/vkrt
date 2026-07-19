#include "assets/shaders/common.hlsli"
#define LOCAL_SIZE 8

struct PushConstants
{
    ENG_UINT dstDepthRWTextureIndex;
    ENG_UINT dstNormalRWTextureIndex;
	ENG_UINT dstVelocityRWTextureIndex;
    ENG_UINT srcDepthTextureIndex;
    ENG_UINT srcNormalTextureIndex;
	ENG_UINT srcVelocityTextureIndex;
};
[[vk::push_constant]] PushConstants pc;

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gidx : SV_GroupIndex)
{
    Texture2D<float> srcDepthTex = get_gt2(srcDepth, float);
    Texture2D<float4> srcNormalTex = get_gt2(srcNormal, float4);
	Texture2D<float2> srcVelocityTex = get_gt2(srcVelocity, float2);
	
    RWTexture2D<float> dstDepthTex = get_grwt2(dstDepth, float);
    RWTexture2D<float4> dstNormalTex = get_grwt2(dstNormal, float4);
	RWTexture2D<float2> dstVelocityTex = get_grwt2(dstVelocity, float2);
    
    uint2 dstDims;
    dstDepthTex.GetDimensions(dstDims.x, dstDims.y);
    
    uint2 srcDims;
    srcDepthTex.GetDimensions(srcDims.x, srcDims.y);
    
    float2 ratio = float2(srcDims.xy) / float2(dstDims.xy);
    
	float2 srcTL = float2(dtid.xy) * ratio;
	float2 srcBR = float2(dtid.xy + 1) * ratio;
	int2 srcTLCoord = int2(floor(srcTL));
	int2 srcBRCoord = int2(ceil(srcBR)) - 1;
	
	if (any(dtid.xy >= dstDims)) { return; }

    float minDepth = 0.0;
	int2 normalCoord;
    
    [unroll]
    for (int y = 0; y < 3; ++y)
    {
        [unroll]
        for (int x = 0; x < 3; ++x)
        {
            int2 coord = srcTLCoord + int2(x, y);
            if(all(coord <= srcBRCoord))
            {
                float d = srcDepthTex[coord].x;
                minDepth = max(minDepth, d); // rev z
				normalCoord = coord;
            }
        }
    }
    
    dstDepthTex[dtid.xy] = minDepth;
    dstNormalTex[dtid.xy] = float4(srcNormalTex[normalCoord].xyz, 0.0);
	dstVelocityTex[dtid.xy] = srcVelocityTex[normalCoord].xy;
}


// groupshared version is actually slower (from 0.03ms to 0.04+-0.01ms)
#if 0
#include "assets/shaders/common.hlsli"
#define LOCAL_SIZE 8

struct PushConstants
{
    ENG_UINT dstDepthRWTextureIndex;
    ENG_UINT srcDepthTextureIndex;
};
[[vk::push_constant]] PushConstants pc;

// because this shader is designed for halfres downscale
// footprint will be 2:1, therefore, if local size is 8, footprint
// is 16x16. add +1 on both sides for odd sizes making the footprint sometimes 3x3.
#define SHARED_FOOTPRINT_SIZE 18
#define TILE_DIM (SHARED_FOOTPRINT_SIZE * SHARED_FOOTPRINT_SIZE)
groupshared float s_depths[TILE_DIM];

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gidx : SV_GroupIndex)
{
    Texture2D<float> srcDepthTex = get_gt2(srcDepth, float);
    RWTexture2D<float> dstDepthTex = get_grwt2(dstDepth, float);
    
    uint2 dstDims;
    dstDepthTex.GetDimensions(dstDims.x, dstDims.y);
    
    uint2 srcDims;
    srcDepthTex.GetDimensions(srcDims.x, srcDims.y);
    
    float2 ratio = float2(srcDims.xy) / float2(dstDims.xy);
    
	int2 groupSrcTL = int2(floor(float2(gid.xy * LOCAL_SIZE) * ratio));
	for(uint i = gidx; i < TILE_DIM; i += (LOCAL_SIZE * LOCAL_SIZE))
	{
		int2 coord = groupSrcTL + int2(i % SHARED_FOOTPRINT_SIZE, i / SHARED_FOOTPRINT_SIZE);
		coord = clamp(coord, int2(0, 0), int2(srcDims.xy - 1));
		s_depths[i] = srcDepthTex[coord].x;
	}
	GroupMemoryBarrierWithGroupSync();
	
    if (any(dtid.xy >= dstDims)) { return; }
	
	float2 srcTL = float2(dtid.xy) * ratio;
	float2 srcBR = float2(dtid.xy + 1) * ratio;
	int2 srcTLCoord = int2(floor(srcTL));
	int2 srcBRCoord = int2(ceil(srcBR)) - 1;
	
    float minDepth = 0.0;
    
    [unroll]
    for (int y = 0; y < 3; ++y)
    {
        [unroll]
        for (int x = 0; x < 3; ++x)
        {
            int2 coord = srcTLCoord + int2(x, y);
			int2 lds_coord = coord - groupSrcTL;
			lds_coord = clamp(lds_coord, int2(0, 0), int2(SHARED_FOOTPRINT_SIZE, SHARED_FOOTPRINT_SIZE) - 1);
			uint lds_idx = lds_coord.y * SHARED_FOOTPRINT_SIZE + lds_coord.x;
            if(all(coord <= srcBRCoord))
            {
                float d = s_depths[lds_idx];
                minDepth = max(minDepth, d); // rev z
            }
        }
    }
    
    dstDepthTex[dtid.xy] = minDepth;
}
#endif