#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

#define h0_image storageImages_2drgba32f[h0]
#define ht_image storageImages_2drg32f[ht]
#define dtx_image storageImages_2drg32f[dtx]
#define dtz_image storageImages_2drg32f[dtz]

void main() {
    vec2 kl = vec2(gl_GlobalInvocationID.xy) - settings.num_samples * 0.5;
    vec2 k = 2.0 * PI * kl / settings.patch_size;
    float k_ = max(length(k), 1e-4);

    vec4 h0 = imageLoad(h0_image, ivec2(gl_GlobalInvocationID.xy));
    Complex h0_pk = Complex(h0.x, h0.y);
    Complex h0_nk = Complex(h0.z, h0.w);
    float wk = sqrt(9.81 * k_);
    Complex cht = cadd(cmul(h0_pk, cexp(time * wk)), cmul(conj(h0_nk), conj(cexp(time * wk))));

    vec2 nk = k / k_;
    imageStore(ht_image, ivec2(gl_GlobalInvocationID.xy), vec4(cht.re, cht.im, 0.0, 1.0));
#if 1
    Complex phaseX = Complex(0.0, -nk.x);
    Complex phaseZ = Complex(0.0, -nk.y);
    Complex dx = cmul(phaseX, cht);
    Complex dz = cmul(phaseZ, cht);

    imageStore(dtx_image, ivec2(gl_GlobalInvocationID.xy), vec4(dx.re, dx.im, 0.0, 1.0));
    imageStore(dtz_image, ivec2(gl_GlobalInvocationID.xy), vec4(dz.re, dz.im, 0.0, 1.0));
#endif
}