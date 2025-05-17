#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

#define ht_image storageImages_2drg32f[ht]
#define dtx_image storageImages_2drg32f[dtx]
#define dtz_image storageImages_2drg32f[dtz]
#define disp_image storageImages_2drgba32f[disp]

void main() {

}