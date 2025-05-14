#version 460 core

#include "./bindless_structures.inc.glsl"

#define PI 3.14159265358979323846

layout(local_size_x = 8, local_size_y = 8) in;

layout(scalar, push_constant) uniform PushConstants {
    FFTOceanSettings settings;
    uint32_t hy_index;
    uint32_t hx_index;
    uint32_t hz_index;
    uint32_t debug_index;
};

ENG_DECLARE_STORAGE_BUFFERS(GPUFFTDebugBuffer) { ENG_TYPE_UINT max; }
ENG_DECLARE_BINDLESS(GPUFFTDebugBuffer);
#define debug_buffer storageBuffers_GPUFFTDebugBuffer[debug_index]
#define hy_image storageImages_2drgba32f[hy_index]
#define hx_image storageImages_2drgba32f[hx_index]
#define hz_image storageImages_2drgba32f[hz_index]

void main() {
    return;
    const ivec2 x = ivec2(gl_GlobalInvocationID.xy);
    const float nx = imageLoad(hx_image, x).r;
    const float ny = imageLoad(hy_image, x).r;
    const float nz = imageLoad(hz_image, x).r;
    const float norm = uintBitsToFloat(debug_buffer.max);
    imageStore(hx_image, x, vec4(vec3(nx / norm), 1.0));
    imageStore(hy_image, x, vec4(vec3(ny / norm), 1.0));
    imageStore(hz_image, x, vec4(vec3(nz / norm), 1.0));
}