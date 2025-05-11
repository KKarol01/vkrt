#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t pingpong0_index;
    uint32_t pingpong1_index;
    uint32_t butterfly_index;
    uint32_t stage;
    uint32_t direction;
    uint32_t pingpong;
};

#define pingpong0_image storageImages_2drg32f[pingpong0_index]
#define pingpong1_image storageImages_2drg32f[pingpong1_index]
#define butterfly_image storageImages_2drgba32f[butterfly_index]

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

Complex horizontal_butterfly() {
    ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    vec4 data = imageLoad(butterfly_image, ivec2(stage, x.x)).rgba;
    if(pingpong == 0) {
        vec2 p_ = imageLoad(pingpong0_image, ivec2(data.z, x.y)).rg;
        vec2 q_ = imageLoad(pingpong0_image, ivec2(data.w, x.y)).rg;
        vec2 w_ = vec2(data.x, data.y);
        Complex p = Complex(p_.x, p_.y);
        Complex q = Complex(q_.x, q_.y);
        Complex w = Complex(w_.x, w_.y);
        return cadd(p, cmul(w, q));
    } else {
        vec2 p_ = imageLoad(pingpong1_image, ivec2(data.z, x.y)).rg;
        vec2 q_ = imageLoad(pingpong1_image, ivec2(data.w, x.y)).rg;
        vec2 w_ = vec2(data.x, data.y);
        Complex p = Complex(p_.x, p_.y);
        Complex q = Complex(q_.x, q_.y);
        Complex w = Complex(w_.x, w_.y);
        return cadd(p, cmul(w, q));
    }
}

Complex vertical_butterfly() {
    ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    vec4 data = imageLoad(butterfly_image, ivec2(stage, x.y)).rgba;
    if(pingpong == 0) {
        vec2 p_ = imageLoad(pingpong0_image, ivec2(x.x, data.z)).rg;
        vec2 q_ = imageLoad(pingpong0_image, ivec2(x.x, data.w)).rg;
        vec2 w_ = vec2(data.x, data.y);
        Complex p = Complex(p_.x, p_.y);
        Complex q = Complex(q_.x, q_.y);
        Complex w = Complex(w_.x, w_.y);
        return cadd(p, cmul(w, q));
    } else {
        vec2 p_ = imageLoad(pingpong1_image, ivec2(x.x, data.z)).rg;
        vec2 q_ = imageLoad(pingpong1_image, ivec2(x.x, data.w)).rg;
        vec2 w_ = vec2(data.x, data.y);
        Complex p = Complex(p_.x, p_.y);
        Complex q = Complex(q_.x, q_.y);
        Complex w = Complex(w_.x, w_.y);    
        return cadd(p, cmul(w, q));
    }
}

void main() {
    Complex c = direction == 0 ? horizontal_butterfly() : vertical_butterfly();
    if(pingpong == 0) {
        imageStore(pingpong1_image, ivec2(gl_GlobalInvocationID.xy), vec4(c.re, c.im, 0.0, 1.0));
    } else {
        imageStore(pingpong0_image, ivec2(gl_GlobalInvocationID.xy), vec4(c.re, c.im, 0.0, 1.0));
    }
}