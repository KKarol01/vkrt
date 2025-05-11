#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 1, local_size_y = 1) in;

layout(scalar, push_constant) uniform PushConstants { uint32_t butterfly_index; };

#define butterfly_image storageImages_2drgba32f[butterfly_index]

uint bit_reverse(uint n, uint bits) {
    uint r = 0;
    for(uint i = 0; i < bits; ++i, n >>= 1) {
        r = (r << 1) | (n & 1);
    }
    return r;
}

void main() {
    const uint stage = uint(gl_GlobalInvocationID.x);
    const uint samples = uint(imageSize(butterfly_image).y);
    const uint y = uint(gl_GlobalInvocationID.y);
    const uint bits = uint(imageSize(butterfly_image).x);

    const vec2 x = gl_GlobalInvocationID.xy;
    const uint span = 2u << stage;
    const float k = mod(x.y * (float(samples) / pow(2.0, x.x + 1.0)), float(samples));
    const float theta = 2.0 * PI * float(k) / float(samples);
    const float re = cos(theta);
    const float im = sin(theta);

    int cond = 0;
    if(mod(x.y, pow(2.0, x.x + 1.0)) < pow(2.0, x.x)) { cond = 1; }

    if(stage == 0) {
        if(cond == 1) {
            imageStore(butterfly_image, ivec2(gl_GlobalInvocationID.xy),
                       vec4(re, im, float(bit_reverse(y, bits)), float(bit_reverse(y + 1, bits))));
        } else {
            imageStore(butterfly_image, ivec2(gl_GlobalInvocationID.xy),
                       vec4(re, im, float(bit_reverse(y - 1, bits)), float(bit_reverse(y, bits))));
        }
    } else {
        if(cond == 1) {
            imageStore(butterfly_image, ivec2(gl_GlobalInvocationID.xy), vec4(re, im, float(y), float(y + span)));
        } else {
            imageStore(butterfly_image, ivec2(gl_GlobalInvocationID.xy), vec4(re, im, float(y - span), float(y)));
        }
    }
}