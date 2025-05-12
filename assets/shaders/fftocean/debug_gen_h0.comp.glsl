#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants { 
	FFTOceanSettings settings;
	uint32_t h0_index;
	uint32_t normal_distribution_index;
};

#define h0_image storageImages_2drgba32f[h0_index]
#define normal_distribution_image storageImages_2drgba32f[normal_distribution_index]

float phillips_spectrum(vec2 k) {
	float k_ = max(length(k), 1e-2);
	vec2 nk = k / k_;
	float wind_speed = length(settings.wind_dir);
	vec2 nw = settings.wind_dir / wind_speed;
	float PhL = wind_speed*wind_speed / 9.8;
	return settings.phillips_const
			* exp(-1.0 / pow(k_ * PhL, 2.0))
			* (1.0 / (k_*k_*k_*k_))
			* pow(dot(nk, nw), 2.0);
}

void main() {
	vec2 kl = vec2(gl_GlobalInvocationID.xy) - settings.num_samples * 0.5;
	vec2 k = 2.0 * PI * kl / settings.patch_size;
	vec4 gaussian_xi = imageLoad(normal_distribution_image, ivec2(gl_GlobalInvocationID.xy));
	vec2 h0_k = phillips_spectrum(k) / sqrt(2.0) * gaussian_xi.xy;
	vec2 h0_nk = phillips_spectrum(-k) / sqrt(2.0) * gaussian_xi.zw;
	imageStore(h0_image, ivec2(gl_GlobalInvocationID.xy), vec4(h0_k.xy, h0_nk.xy));
}