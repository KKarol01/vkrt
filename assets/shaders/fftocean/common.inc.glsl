#ifndef FFTOCEAN_COMMON_H
#define FFTOCEAN_COMMON_H

#define PI 3.14159265358979323846
#define NUM_SAMPLES 512

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.inc.glsl"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t gaussian;
    uint32_t h0;
    uint32_t ht;
    uint32_t dtx;
    uint32_t dtz;
    uint32_t dft;
    uint32_t disp;
    uint32_t grad;
    float time;
};
#endif

struct Complex {
    float re;
    float im;
};

Complex cmul(Complex a, Complex b) { return Complex(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }

Complex cadd(Complex a, Complex b) { return Complex(a.re + b.re, a.im + b.im); }

Complex cexp(float v) { return Complex(cos(v), sin(v)); }

Complex conj(Complex a) { return Complex(a.re, -a.im); }

#endif