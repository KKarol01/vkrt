#version 460 core
#extension GL_EXT_scalar_block_layout : enable

// TODO: rename in/out variables to the same convention and order them by in -> out

layout(binding = 0, set = 0, rgba32f) uniform image2D view_positions;
layout(binding = 1, set = 0, r32f) uniform image2D view_ao;
layout(binding = 2, set = 0) uniform sampler2D color_buffer;

layout(location = 0) in VsOut {
	vec3 pos;
} vs_out;

layout(scalar, push_constant) uniform PushConstants {
	mat4 projection;
	vec2 ao_resolution;
};

layout(location = 0) out vec4 fs_out_color;

const float PI = 3.14159265359;
const float ONE_OVER_PI = 1.0 / PI;
const float TWO_PI = 2.0 * PI;
float gaussian_kernel(float x, float sigma) {
	const float one_over_two_sigma2 = 1.0 / (2.0 * sigma * sigma);
	return one_over_two_sigma2 * ONE_OVER_PI * exp(-x*x * one_over_two_sigma2);
}

void main() {
	vec2 uv = vs_out.pos.xy * 0.5 + 0.5;
	ivec2 uv_texel = ivec2(uv * ao_resolution);
	vec3 position = imageLoad(view_positions, uv_texel).rgb;
	float ao_x = imageLoad(view_ao, uv_texel).r;
	vec3 color = texture(color_buffer, uv).rgb;
	
	float result = 0.0;
	float weights = 0.0;
	int samples = 3;
	
	for(int i=-samples; i<=samples; ++i) {
		for(int j=-samples; j<=samples; ++j) {
			vec3 pos = imageLoad(view_positions, uv_texel + ivec2(i, j)).rgb;
			float ao = imageLoad(view_ao, uv_texel + ivec2(i, j)).r;
			float diff = abs(ao_x - ao);
			float weight1 = gaussian_kernel(abs(ao_x - ao), 0.8);
			float weight2 = gaussian_kernel(distance(position, pos), 0.05);
			result += ao * weight1 * weight2;
			weights += weight1 * weight2;
		}
	}
	result /= weights;
	//fs_out_color = vec4(vec3(result * color), 1.0);
	fs_out_color = vec4(vec3(color), 1.0);
}
