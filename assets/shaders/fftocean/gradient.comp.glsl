#version 460 core

#include "./fftocean/common.inc.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

#define disp_image storageImages_2drgba32f[disp]
#define grad_image storageImages_2drgba32f[grad]

void main() {
    ivec2 x = ivec2(gl_GlobalInvocationID.xy);

    ivec2 left = (x - ivec2(1, 0)) & (int(settings.num_samples) - 1);
    ivec2 right = (x - ivec2(-1, 0)) & (int(settings.num_samples) - 1);
    ivec2 top = (x - ivec2(-1, 0)) & (int(settings.num_samples) - 1);
    ivec2 bottom = (x - ivec2(1, 0)) & (int(settings.num_samples) - 1);

    vec3 disp_left = imageLoad(disp_image, left).xyz;
    vec3 disp_right = imageLoad(disp_image, right).xyz;
    vec3 disp_top = imageLoad(disp_image, top).xyz;
    vec3 disp_bottom = imageLoad(disp_image, bottom).xyz;

    vec2 gradient = vec2(disp_left.y - disp_right.y, disp_bottom.y - disp_top.y);
    float inv_patch_size = 1.0 / settings.patch_size;
    vec2 dDx = (disp_right.xz - disp_left.xz) * inv_patch_size;
    vec2 dDy = (disp_top.xz - disp_bottom.xz) * inv_patch_size;
    float J = (1.0 + settings.disp_lambda * dDx.x) * (1.0 + settings.disp_lambda * dDy.y) -
              settings.disp_lambda * dDx.y * settings.disp_lambda * dDy.x;

    imageStore(grad_image, x, vec4(gradient.x, gradient.y, J, 1.0));
}