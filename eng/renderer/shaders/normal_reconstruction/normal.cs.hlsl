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
void main(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex)
{
    RWTexture2D<float4> normal = gRWTexture2Df4s[pc.NormalImageIndex];
    Texture2D<float4> depth = gTexture2Ds[pc.DepthImageIndex];
    uint2 normal_dims;
    normal.GetDimensions(normal_dims.x, normal_dims.y);
	
    if (any(dtid.xy >= normal_dims)) { return; }
    
	const float3 cross_pos[5] = {
		depth_to_view_pos(dtid.xy + int2(0, 0),  normal_dims, depth.Load(dtid + int3(0, 0, 0)).x),
		depth_to_view_pos(dtid.xy + int2(1, 0),  normal_dims, depth.Load(dtid + int3(1, 0, 0)).x),
		depth_to_view_pos(dtid.xy + int2(-1, 0), normal_dims, depth.Load(dtid + int3(-1, 0, 0)).x),
		depth_to_view_pos(dtid.xy + int2(0, 1),  normal_dims, depth.Load(dtid + int3(0, 1, 0)).x),
		depth_to_view_pos(dtid.xy + int2(0, -1), normal_dims, depth.Load(dtid + int3(0, -1, 0)).x),
	};
	
	const float z0 = cross_pos[0].z;

	const uint best_Z_horizontal = (abs(cross_pos[1].z - z0) < abs(cross_pos[2].z - z0)) ? 1 : 2;
	const uint best_Z_vertical   = (abs(cross_pos[3].z - z0) < abs(cross_pos[4].z - z0)) ? 3 : 4;

	float3 P1 = 0, P2 = 0;

	if (best_Z_horizontal == 1 && best_Z_vertical == 4)
	{
		P1 = cross_pos[1];
		P2 = cross_pos[4];
	}
	else if (best_Z_horizontal == 1 && best_Z_vertical == 3)
	{
		P1 = cross_pos[3];
		P2 = cross_pos[1];
	}
	else if (best_Z_horizontal == 2 && best_Z_vertical == 4)
	{
		P1 = cross_pos[4];
		P2 = cross_pos[2];
	}
	else if (best_Z_horizontal == 2 && best_Z_vertical == 3)
	{
		P1 = cross_pos[2];
		P2 = cross_pos[3];
	}

	// 3. Final Normal calculation using your center point P0
	const float3 P0 = cross_pos[0];
	
	const bool find_best = true;
	float3 n;
	if(find_best) {
		n = normalize(cross(P2 - P0, P1 - P0));
	} else {
		P1 = cross_pos[1];
		P2 = cross_pos[3];
		n = normalize(cross(P1 - P0, P2 - P0));
	}
	 
	normal[dtid.xy] = float4(n, 1.0); 
}