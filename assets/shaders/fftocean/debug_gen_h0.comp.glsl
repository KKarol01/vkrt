#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

#define h0_image storageImages_2drgba32f[h0]
#define normal_distribution_image storageImages_2drgba32f[gaussian]

float phillips_spectrum(vec2 k) {
    float k_ = max(length(k), 1e-5);
    vec2 nk = k / k_;
    float wind_speed = length(settings.wind_dir);
    vec2 nw = settings.wind_dir / wind_speed;
    float PhL = (wind_speed * wind_speed / 9.8);
    float ell = settings.small_l;
    float ph = settings.phillips_const
             * exp(-1.0 / pow(k_ * PhL, 2.0))
             * (1.0 / pow(k_, 4.0))
             * pow(dot(nk, nw), 2.0)
             * exp(-k_ * k_ * ell * ell);
    
    return sqrt(ph);
}

void main() {
	vec2 kl = vec2(gl_GlobalInvocationID.xy) - settings.num_samples * 0.5;
	vec2 k = 2.0 * PI * kl / settings.patch_size;
	vec4 gaussian_xi = imageLoad(normal_distribution_image, ivec2(gl_GlobalInvocationID.xy));
	vec2 h0_k = phillips_spectrum(k) / sqrt(2.0) * gaussian_xi.xy;
	vec2 h0_nk = phillips_spectrum(-k) / sqrt(2.0) * gaussian_xi.zw;
	imageStore(h0_image, ivec2(gl_GlobalInvocationID.xy), vec4(h0_k.xy, h0_nk.xy));
}