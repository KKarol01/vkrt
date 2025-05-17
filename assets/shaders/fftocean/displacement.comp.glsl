#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

#define ht_image storageImages_2drg32f[ht]
#define dtx_image storageImages_2drg32f[dtx]
#define dtz_image storageImages_2drg32f[dtz]
#define disp_image storageImages_2drgba32f[disp]

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);

    // checkerboard sign correction due to DFT interval
    float sign = (((coord.x + coord.y) & 1) == 1) ? -1.0 : 1.0;
    // load height and choppy displacements
    float h  = sign * imageLoad(ht_image, coord).x;
    float dx = sign * imageLoad(dtx_image, coord).x;
    //float dz = sign * imageLoad(dtx_image, coord).y;
    float dz = sign * imageLoad(dtz_image, coord).x;

    float lambda = settings.disp_lambda;

    imageStore(disp_image, coord, vec4(dx * lambda, h, dz * lambda, 1.0));
}