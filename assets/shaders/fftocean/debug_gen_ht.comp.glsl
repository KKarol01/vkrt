#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants { 
	FFTOceanSettings settings;
	uint32_t h0_index;
	uint32_t hx_index;
	uint32_t hz_index;
	float time;
};

#define h0_image storageImages_2drgba32f[h0_index]
#define hx_image storageImages_2drgba32f[hx_index]
#define hz_image storageImages_2drgba32f[hz_index]
#define normal_distribution_image storageImages_2drgba32f[normal_distribution_index]

struct Complex {
    float re;
    float im;
};

Complex cmul(Complex a, Complex b) {
    Complex c;
    c.re = a.re * b.re - a.im * b.im;
    c.im = a.re * b.im + a.im * b.re;
    return c;
}

Complex cadd(Complex a, Complex b) {
    Complex c;
    c.re = a.re + b.re;
    c.im = a.im + b.im;
    return c;
}

Complex cexp(float v) { return Complex(cos(v), sin(v)); }

Complex conj(Complex a) { return Complex(a.re, -a.im); }

void main() {
	vec2 kl = vec2(gl_GlobalInvocationID.xy) - settings.num_samples * 0.5;
	vec2 k = 2.0 * PI * kl / settings.patch_size;
    float k_ = max(length(k), 1e-5);

	vec4 h0 = imageLoad(h0_image, ivec2(gl_GlobalInvocationID.xy));
	Complex h0_pk = Complex(h0.x, h0.y);
	Complex h0_nk = Complex(h0.z, h0.w);
    float wk = 9.8 * k_;
    Complex ht = cadd(cmul(h0_pk, cexp(time * wk)), cmul(conj(h0_nk), cexp(-time * wk)));
    Complex dx = Complex(0.0, -k.x / k_);
    dx = cmul(dx, ht);
    Complex dz = Complex(0.0, -k.y / k_);
    dz = cmul(dz, ht);
	imageStore(h0_image, ivec2(gl_GlobalInvocationID.xy), vec4(ht.re, ht.im, 0.0, 1.0));
	imageStore(hx_image, ivec2(gl_GlobalInvocationID.xy), vec4(dx.re, dx.im, 0.0, 1.0));
	imageStore(hz_image, ivec2(gl_GlobalInvocationID.xy), vec4(dz.re, dz.im, 0.0, 1.0));
}