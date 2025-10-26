#version 460

#include "./culling/common.glsli"

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

void main()
{
    const uvec2 x = gl_GlobalInvocationID.xy;
    const uvec2 sz = uvec2(imageSize(gsi_2dr32f[hizdstii]).xy);
    if(any(greaterThanEqual(x, sz))) { return; }
    const float depth = texture(sampler2D(gt_2d[hizsrcti], g_samplers[ENG_SAMPLER_HIZ]), (vec2(x) + vec2(0.5)) / vec2(sz)).x;
    imageStore(gsi_2dr32f[hizdstii], ivec2(x), vec4(depth));
}