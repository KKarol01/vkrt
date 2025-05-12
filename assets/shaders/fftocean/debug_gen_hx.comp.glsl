#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t ht_index;
    uint32_t hx_index;
    uint32_t debug_index;
};

#define ht_image storageImages_2drgba32f[ht_index]
#define hx_image storageImages_2drgba32f[hx_index]

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
    float N = settings.num_samples;
    float L = settings.patch_size;
    vec2 x = vec2(gl_GlobalInvocationID.xy);
    vec2 k = (2.0 * PI * x - PI * N) / L;

    Complex sum = Complex(0.0, 0.0);
    for(float i = 0.0; i < N; i += 1.0) {
        Complex sum_0 = Complex(0.0, 0.0);
        for(float j = 0.0; j < N; j += 1.0) {
            vec2 ht = imageLoad(ht_image, ivec2(i, j)).rg;
            Complex cht = Complex(ht.x, ht.y);
            sum_0 = cadd(sum_0, cmul(cht, cexp((2.0 * PI * j * x.x - PI * N * x.x) / N)));
        }
        sum_0 = cmul(sum_0, cexp((2.0 * PI * i * x.y - PI * N * x.y) / N));
        sum = cadd(sum, sum_0);
    }
    sum.re /= N * N;
    // sum.im /= N * N;
    imageStore(hx_image, ivec2(x), vec4(vec3(sum.re), 1.0));
}