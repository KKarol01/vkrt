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

    OUT_COLOR = vec4(vec2(tuv) / vec2(tc), lgc, 1.0);
}
