#version 460

#include "./culling/common.glsli"

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

void main()
{
#if 0
    const uvec2 x = gl_GlobalInvocationID.xy;
    const uvec2 sz = uvec2(hiz_width, hiz_height);
   // if(any(greaterThanEqual(x, sz))) { return; }
    const float depth = texture(hiz_source, (vec2(x) + vec2(0.5)) / vec2(sz)).x;
    imageStore(hiz_dest, ivec2(x), vec4(depth));
#endif
}