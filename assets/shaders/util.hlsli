#ifndef UTIL_COMMON_H
#define UTIL_COMMON_H 

/*
	x,y in -1..1, z is what was in zbuffer at x,y
	inf_revz is a matrix that has no far plane (in my case +Y points down, because vulkan), 
	it looks like this (col major, but hlsl has row major access):
	[a,  0,  0,  0	] [x]  	
	[0, -b,  0,  0  ] [y]  	
	[0,  0,  0, near] [z]    		
	[0,  0, -1   0	] [w]
	so:
	x' = ax, 
	y' = -by, (-, because Y is down for vulkan)
	z' = near
	w' = -z (-, because -Z is forward -- OpenGL notation)
	after perspective divide, z' becomes near/-z,
	and so z is 1...0 for all negative 
*/
float3 unproject_inf_revz_depth(float3 ndc) 
{
	const float4x4 proj = get_grwb(GPUEngConstants, 0).proj;
	const float z = -proj[2][3] / ndc.z;
	return float3(
		(ndc.x * -z) / proj[0][0],
		(ndc.y * -z) / proj[1][1],
		z
	);
}

float3 unproject_inf_revz_depth(float2 uv, Texture2D<float> depth_texture) 
{
	return unproject_inf_revz_depth(float3(uv * 2.0 - 1.0, depth_texture.SampleLevel(gSamplerNearest, uv, 0)));
}

float3 depth_to_view_pos(uint2 coords, uint2 dims, float depth)
{
    float2 uv = (float2(coords) + 0.5) / float2(dims);
    float2 ndc_xy = uv * 2.0 - 1.0;
	return unproject_inf_revz_depth(float3(ndc_xy.xy, depth)); 
}

float3 calculate_normal_from_depth(int2 coords, int2 dimensions, Texture2D<float> depthTexture)
{
    // Load center and 4 neighbors
    float3 p0 = depth_to_view_pos(coords, dimensions, depthTexture.Load(int3(coords, 0)).x);
    float3 pR = depth_to_view_pos(coords + int2(1, 0),  dimensions, depthTexture.Load(int3(coords + int2(1, 0), 0)).x);
    float3 pL = depth_to_view_pos(coords + int2(-1, 0), dimensions, depthTexture.Load(int3(coords + int2(-1, 0), 0)).x);
    float3 pD = depth_to_view_pos(coords + int2(0, 1),  dimensions, depthTexture.Load(int3(coords + int2(0, 1), 0)).x);
    float3 pU = depth_to_view_pos(coords + int2(0, -1), dimensions, depthTexture.Load(int3(coords + int2(0, -1), 0)).x);

    // Pick best horizontal and vertical vectors based on smallest depth delta
    float3 vH = (abs(pR.z - p0.z) < abs(pL.z - p0.z)) ? (pR - p0) : (p0 - pL);
    float3 vV = (abs(pD.z - p0.z) < abs(pU.z - p0.z)) ? (pD - p0) : (p0 - pU);

    // Right-Handed (+X Right, +Y Down): Normal = Horizontal x Vertical
    // This results in +Z pointing toward the camera.
    return normalize(cross(vH, vV)); 
}

// https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
float IGN(int x, int y)
{
    return fmod(52.9829189f * fmod(0.06711056f * float(x) + 0.00583715f * float(y), 1.0f), 1.0f);
}


#endif