#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t pingpong0_index;
    uint32_t pingpong1_index;
    uint32_t displacement_index;
    uint32_t pingpong;
};

#define pingpong0_image storageImages_2drg32f[pingpong0_index]
#define pingpong1_image storageImages_2drg32f[pingpong1_index]
#define displacement_image storageImages_2drgba32f[displacement_index]

const float perms[] = { 1.0, -1.0 };

void main() {
    ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    float perm = perms[int(x.x + x.y) % 2];
    if(pingpong == 0) {
        float h = imageLoad(pingpong0_image, x).r * perm / (settings.num_samples * settings.num_samples);
        imageStore(displacement_image, x, vec4(vec3(h), 1.0));
    } else {
        float h = imageLoad(pingpong1_image, x).r * perm / (settings.num_samples * settings.num_samples);
        imageStore(displacement_image, x, vec4(vec3(h), 1.0));
    }
}