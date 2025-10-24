#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) in VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    flat uint32_t iidx;
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
 	uvec2 ss = uvec2(1280, 768);
	uint ts = 16;
	uvec2 tc = uvec2(ss + uvec2(ts - 1)) / uvec2(ts);
    uvec2 tuv = uvec2(gl_FragCoord.xy / vec2(ts));
    uint tx = tuv.x + tuv.y * tc.x;
    
    float lgc = float(get_buf(GPUFWDPLightGrid).grids_us[tx].y);

    uint matidx = get_buf2(GPUMeshletId, imidb).ids_us[imidb].matidx;
    GPUMaterial mat = get_buf2(GPUMaterial, get_buf(GPUEngConstant).rmatb).materials_us[matidx];

    vec4 color = vec4(vec3(colors[fsin.iidx % 10]), 1.0);
    if(lgc > 0.0 && mat.base_color_idx != ~0)
    {
    //    color = texture(sampler2D(gt_2d[mat.base_color_idx], g_samplers[ENG_SAMPLER_LINEAR]), fsin.uv);
    }

    GPULight l0 = get_bufb(GPULight, get_buf(GPUEngConstant)).lights_us[0];
    float att = 1.0;

    if(lgc > 0.0)
    {
        //const float d = distance(l0.pos, fsin.position);
        //att = clamp(1.0 - d / l0.range, 0.0, 1.0);
        //att = att * att * l0.intensity * max(0.0, dot(fsin.normal, normalize(l0.pos - fsin.position))); // could be divided by d, instead of normalizing?
        //color *= l0.color;
        //color.xyz *= att;
    }

    //OUT_COLOR = color;
    OUT_COLOR = color;

}
