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
	const float4x4 proj = get_gsb(GPUEngConstants, 0).proj;
	const float z = -proj[2][3] / ndc.z;
	return float3(
		(ndc.x * -z) / proj[0][0],
		(ndc.y * -z) / proj[1][1],
		z
	);
}


#endif