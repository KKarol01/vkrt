#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t hy_index;
    uint32_t hx_index;
    uint32_t hz_index;
    uint32_t hn_index;
    uint32_t debug_index;
};

ENG_DECLARE_STORAGE_BUFFERS(GPUFFTDebugBuffer) { ENG_TYPE_UINT max; }
ENG_DECLARE_BINDLESS(GPUFFTDebugBuffer);
#define debug_buffer storageBuffers_GPUFFTDebugBuffer[debug_index]
#define hy_image storageImages_2drgba32f[hy_index]
#define hx_image storageImages_2drgba32f[hx_index]
#define hz_image storageImages_2drgba32f[hz_index]
#define hn_image storageImages_2drgba32f[hn_index]

shared uint s_maxval;

void main() {
    const ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    const float nx = imageLoad(hx_image, x).r;
    const float ny = imageLoad(hy_image, x).r;
    const float nz = imageLoad(hz_image, x).r;
    imageStore(hn_image, x, vec4(normalize(vec3(nx, ny, nz)), 1.0));

    const uint idx = uint(gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_WorkGroupSize.x * gl_NumWorkGroups.x);
    const float my = abs(ny);
    if(idx == 0) { s_maxval = 0u; }
    barrier();
    atomicMax(s_maxval, floatBitsToUint(my));
    barrier();
    if(idx == 0) { atomicMax(debug_buffer.max, s_maxval); }
}