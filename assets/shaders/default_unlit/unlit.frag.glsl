#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) in VsOut {
    vec3 position;
    // vec3 normal;
    // vec2 uv;
    // flat uint32_t iidx;
} fsin;

layout(location = 0) out vec4 OUT_COLOR;

const vec3 colors[10] = vec3[](
    vec3(1.0, 0.0, 0.0),  // red
    vec3(0.0, 1.0, 0.0),  // green
    vec3(0.0, 0.0, 1.0),  // blue
    vec3(1.0, 1.0, 0.0),  // yellow
    vec3(1.0, 0.0, 1.0),  // magenta
    vec3(0.0, 1.0, 1.0),  // cyan
    vec3(1.0, 0.5, 0.0),  // orange
    vec3(0.5, 0.0, 1.0),  // purple
    vec3(0.0, 0.5, 0.5),  // teal
    vec3(0.5, 0.5, 0.5)   // gray
);

void main() 
{

	OUT_COLOR = vec4(1.0);

#if 0
 	uvec2 ss = uvec2(1280, 768);
	uint ts = 16;
	uvec2 tc = uvec2(ss + uvec2(ts - 1)) / uvec2(ts);
    uvec2 tuv = uvec2(gl_FragCoord.xy / vec2(ts));
    uint tx = tuv.x + tuv.y * tc.x;
    
    uint lloff = get_buf(GPUFWDPLightGrid).grids_us[tx].x;
    uint lgc = get_buf(GPUFWDPLightGrid).grids_us[tx].y;
    const bool fwdp_enable = get_buf(GPUEngConstant).fwdp_enable == 1;
    if(!fwdp_enable)
    {
        lgc = get_bufb(GPULight, get_buf(GPUEngConstant)).count;
    }

    uint matidx = get_buf2(GPUInstanceId, imidb).ids_us[fsin.iidx].mati;
    GPUMaterial mat = get_buf2(GPUMaterial, get_buf(GPUEngConstant).rmatb).materials_us[matidx];

    vec4 color = vec4(vec3(colors[fsin.iidx % 10]), 1.0);
    if(lgc > 0.0 && mat.base_color_idx != ~0)
    {
    //    color = texture(sampler2D(gt_2d[mat.base_color_idx], g_samplers[ENG_SAMPLER_LINEAR]), fsin.uv);
    }


    vec3 lighting = vec3(0.0);
    for (uint i = 0; i < lgc; ++i)
    {
        GPULight l0;
        if(fwdp_enable) 
        {
            const uint lidx = get_buf(GPUFWDPLightList).lights_us[lloff + i];
            l0 = get_bufb(GPULight, get_buf(GPUEngConstant)).lights_us[lidx];
        } else { l0 = get_bufb(GPULight, get_buf(GPUEngConstant)).lights_us[i]; }
        
        vec3 L = l0.pos - fsin.position;
        float d = length(L);
        vec3 Ldir = L / d;

        float att = clamp(1.0 - d / l0.range, 0.0, 1.0);
        att = att * att * l0.intensity;

        float NdotL = max(dot(fsin.normal, Ldir), 0.0);
        lighting += l0.color.xyz * (att * NdotL);
    }
    color.rgb *= lighting;

    //OUT_COLOR = color;
    const uint output_mode = get_buf(GPUEngConstant).output_mode;
    if(output_mode == 0)
    {
        OUT_COLOR = color;
    }
    else if(output_mode == 1)
    {
        OUT_COLOR.rgb = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), lgc/64.0);
        OUT_COLOR.a = 1.0;
    }
    // OUT_COLOR = vec4(mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), lgc / 4.0), 1.0);

	#endif
}
