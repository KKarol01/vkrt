#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = NUM_SAMPLES) in;

#define read_image storageImages_2drg32f[ht]
#define write_image storageImages_2drg32f[dft]

shared vec2 s_values[NUM_SAMPLES];

void main() {
    const ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 nm = ivec2(gl_WorkGroupID.x, gl_LocalInvocationID.x);
    
    s_values[nm.y] = imageLoad(read_image, nm).rg;
    memoryBarrierShared();
    barrier();

    vec2 result = vec2(0.0);
    int i_num_samples = int(settings.num_samples);
    for(int k=0; k < i_num_samples; ++k) {
        vec2 coeff = s_values[k];

        float theta = (nm.y * PI * 2.0 * float(k)) / settings.num_samples;
        float cost = cos(theta);
        float sint = sin(theta);

        result.x += coeff.x * cost + coeff.y * sint;
        result.y += coeff.y * cost - coeff.x * sint;
    }

    imageStore(write_image, nm.yx, vec4(result.xy, 0.0, 1.0));
}