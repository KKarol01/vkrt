#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t amplitudes_index;
    uint32_t fourier_amps_index;
    float time;
};

#define amplitudes_image storageImages_2drgba32f[amplitudes_index]
#define fourier_amps_image storageImages_2drg32f[fourier_amps_index]

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
    vec4 h0s = imageLoad(amplitudes_image, ivec2(gl_GlobalInvocationID.xy));
    Complex h0_k = Complex(h0s.x, h0s.y);
    Complex h0_nk = Complex(h0s.z, h0s.w);
    float w = sqrt(9.8 * k_);
    Complex hkt = cadd(cmul(h0_k, cexp(w * time)), cmul(conj(h0_nk), cexp(-w * time)));
    imageStore(fourier_amps_image, ivec2(gl_GlobalInvocationID.xy), vec4(hkt.re, hkt.im, 0.0, 0.0));
}