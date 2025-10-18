#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = NUM_SAMPLES) in;

#define read_image gsi_2drg32f[ht]
#define write_image gsi_2drg32f[dft]

shared vec2 s_values[NUM_SAMPLES];

void main() {
    const ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 nm = ivec2(gl_WorkGroupID.x, gl_LocalInvocationID.x);

    s_values[nm.y] = imageLoad(read_image, nm).rg;
    memoryBarrierShared();
    barrier();

    Complex result = Complex(0.0, 0.0);
    for(float k = 0.0; k < settings.num_samples; k += 1.0) {
        Complex coeff = Complex(s_values[int(k)].x, s_values[int(k)].y);
        float theta = (2.0 * PI * nm.y * k) / settings.num_samples;
        result = cadd(result, cmul(coeff, cexp(theta)));
    }

    imageStore(write_image, nm.yx, vec4(result.re, result.im, 0.0, 1.0));
}