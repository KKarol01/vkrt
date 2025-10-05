#version 460

#include "./culling/common.glsli"

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

void main()
{
    const uvec2 x = gl_GlobalInvocationID.xy;
    const uvec2 sz = uvec2(imageSize(hizdst).xy);
    // if(any(greaterThanEqual(x, sz))) { return; }
    const float depth = texture(sampler2D(hizsrc, samplers[G_SAMPLER_LINEAR]), (vec2(x) + vec2(0.5)) / vec2(sz)).x;
    imageStore(hizdst, ivec2(x), vec4(depth));
}