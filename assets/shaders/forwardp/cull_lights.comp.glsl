#version 460

#include "./forwardp/common.glsli"

// make 16 not hardcoded, if needed -- compile with compile constants maybe
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

shared uint sh_mindepth;
shared uint sh_maxdepth;
shared uint sh_lightcnt;
shared uint sh_lights[256];
shared vec3 sh_frust[4];

vec3 clip2vs(vec4 p)
{
	vec4 view = get_buf(GPUEngConstant).inv_proj * p;
	view /= view.w;
	return view.xyz;	
}

vec3 ss2vs(vec4 p)
{
	vec2 uv = p.xy / imageSize(gsi_2dr32f[depth_texture_index]);
	vec4 clip = vec4(vec2(uv.x, uv.y) * 2.0 - 1.0, p.z, p.w);
	return clip2vs(clip);
}

void main()
{
	const uvec2 gsz = gl_WorkGroupSize.xy;
	const uvec2 gidxy = gl_WorkGroupID.xy;
	const uvec2 gcnt = gl_NumWorkGroups.xy;
	const uvec2 glidxy = gl_LocalInvocationID.xy;
	const uvec2 ggidxy = gl_GlobalInvocationID.xy;
	const uint glid = glidxy.x + glidxy.y*gsz.x;
	const ivec2 dimgsize = imageSize(gsi_2dr32f[depth_texture_index]).xy;

	if(any(greaterThanEqual(ivec2(ggidxy), dimgsize))) { return; }

	if(glidxy.x == 0 && glidxy.y == 0)
	{
		sh_mindepth = floatBitsToUint(0.0);
		sh_maxdepth = floatBitsToUint(1.0);
		sh_lightcnt = 0;
	}
	barrier();

	if(glidxy.x == 0 && glidxy.y == 0)
	{
		const vec3 viewps[4] = vec3[](
			ss2vs(vec4(vec2(gidxy.x,   gidxy.y)     * gsz, -1.0, 1.0)).xyz,
			ss2vs(vec4(vec2(gidxy.x+1, gidxy.y)     * gsz, -1.0, 1.0)).xyz,
			ss2vs(vec4(vec2(gidxy.x,   gidxy.y + 1) * gsz, -1.0, 1.0)).xyz,
			ss2vs(vec4(vec2(gidxy.x+1, gidxy.y + 1) * gsz, -1.0, 1.0)).xyz
		);
		sh_frust[0] = normalize(cross(viewps[2], viewps[0]));
		sh_frust[1] = normalize(cross(viewps[1], viewps[3]));
		sh_frust[2] = normalize(cross(viewps[0], viewps[1]));
		sh_frust[3] = normalize(cross(viewps[3], viewps[2]));
	}
	barrier();

	const ivec2 depthcoords = ivec2(ggidxy);
	const float d = imageLoad(gsi_2dr32f[depth_texture_index], depthcoords).r;
	atomicMax(sh_mindepth, floatBitsToUint(d));
	atomicMin(sh_maxdepth, floatBitsToUint(d));
	barrier();
		
	const float mindvs = clip2vs(vec4(0.0, 0.0, uintBitsToFloat(sh_mindepth), 1.0)).z;
	const float maxdvs = clip2vs(vec4(0.0, 0.0, uintBitsToFloat(sh_maxdepth), 1.0)).z;
	const float nearvs = clip2vs(vec4(0.0, 0.0, 1.0, 1.0)).z;
	const uint lc = get_bufb(GPULight, get_buf(GPUEngConstant)).count;
	for(uint i=glid; i<lc; i+=gsz.x*gsz.y)
	{
		GPULight l = get_bufb(GPULight, get_buf(GPUEngConstant)).lights_us[i];
		const vec3 lvs = vec3(get_buf(GPUEngConstant).view * vec4(l.pos, 1.0));
		bool passed = !((lvs.z - l.range > mindvs) || (lvs.z + l.range < maxdvs));
		for(int pi=0; passed && pi<4; ++pi)
		{
			if(dot(sh_frust[pi], lvs) < -l.range) { passed = false; }
		}
		if(passed)
		{
			const uint passed_idx = atomicAdd(sh_lightcnt, 1);
			if(passed_idx < 64) { sh_lights[passed_idx] = i; }
		}
	}
	barrier();
	
	if(glidxy.x == 0 && glidxy.y == 0)
	{
		const uint list_offset = atomicAdd(get_buf(GPUFWDPLightList).head, sh_lightcnt);
		const uint lightcnt = min(sh_lightcnt, get_buf(GPUFWDPLightList).max_lights_per_tile);
		for(int i=0; i<lightcnt; ++i)
		{
			get_buf(GPUFWDPLightList).lights_us[list_offset + i] = sh_lights[i];
		}
		get_buf(GPUFWDPLightGrid).grids_us[gidxy.x + gidxy.y*gcnt.x].xy = uvec2(list_offset, sh_lightcnt);
	}
}