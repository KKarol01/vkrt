#version 460

#include "./forwardp/common.glsli"

// make 16 not hardcoded, if needed -- compile with compile constants maybe
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

uvec2 ss = uvec2(1280, 768);
uint ts = 16;
uvec2 tc = uvec2(ss + uvec2(ts - 1)) / uvec2(ts);
uint ttc = tc.x * tc.y;

vec3 clip2vs(vec4 p)
{
	vec4 view = get_buf(GPUEngConstant).inv_proj * p;
	view /= view.w;
	return view.xyz;	
}

vec3 ss2vs(vec4 p)
{
	vec2 uv = p.xy / vec2(ss);
	vec4 clip = vec4(vec2(uv.x, uv.y) * 2.0 - 1.0, p.z, p.w);
	return clip2vs(clip);
}

void main()
{
	for(uint i=0; i<ttc; ++i)
	{
		uvec2 tuv = uvec2(i % tc.x, i / tc.x);
		float mind = 0.0;
		float maxd = 1.0;
		for(uint dx = 0; dx < 16; ++dx)
		{
			for(uint dy = 0; dy < 16; ++dy)
			{
				float d = imageLoad(gsi_2dr32f[depth_texture_index], ivec2(tuv + uvec2(dx, dy))).r;
				mind = max(mind, d);
				maxd = min(maxd, d);
			}
		}
		vec3 mindvs = clip2vs(vec4(0.0, 0.0, mind, 1.0));
		vec3 maxdvs = clip2vs(vec4(0.0, 0.0, maxd, 1.0));

		vec3 viewps[4] = vec3[](
			ss2vs(vec4(vec2(tuv.x,   tuv.y + 1) * ts, 1.0, 1.0)).xyz,
			ss2vs(vec4(vec2(tuv.x+1, tuv.y + 1) * ts, 1.0, 1.0)).xyz,
			ss2vs(vec4(vec2(tuv.x,   tuv.y) * ts,	  1.0, 1.0)).xyz,
			ss2vs(vec4(vec2(tuv.x+1, tuv.y) * ts,	  1.0, 1.0)).xyz
		);

		vec3 planes[4] = vec3[](
			normalize(cross(viewps[2], viewps[0])),
			normalize(cross(viewps[1], viewps[3])),
			normalize(cross(viewps[0], viewps[1])),
			normalize(cross(viewps[3], viewps[2]))
		);

		GPULight l0 = get_bufb(GPULight, get_buf(GPUEngConstant)).lights_us[0];
		vec3 lvs = vec3(get_buf(GPUEngConstant).view * vec4(l0.pos, 1.0));
		uint passed = 0;
		if(lvs.z - l0.range < mindvs.z || lvs.z + l0.range > maxdvs.z) { passed = 1; }
		if(passed > 0)
		{
			passed = 0;
			for(int i=0; i<4; ++i, ++passed)
			{
				if(dot(planes[i], lvs) < -l0.range) { break; }
			}
		}
		if(passed == 4 && (maxd - mind) < 1.0) { get_buf(GPUFWDPLightGrid).grids_us[i].y = 1; }
		else { get_buf(GPUFWDPLightGrid).grids_us[i].y = 0; }
	}

}