#ifndef UNLIT_COMMON_H
#define UNLIT_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.inc.glsl"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants {
    uint32_t indices_index;
    uint32_t vertex_positions_index;
    uint32_t vertex_attributes_index;
    uint32_t transforms_index;
    uint32_t constants_index;
    uint32_t meshes_index;
    uint32_t vsm_buffer_index;
    uint32_t vsm_physical_depth_image_index;
    uint32_t page_table_index;
    uint32_t fft_fourier_amplitudes_index;
    uint32_t fft_fourier_x_amplitudes_index;
    uint32_t fft_fourier_z_amplitudes_index;
    uint32_t fft_fourier_n_amplitudes_index;
};
#endif

#define NO_PUSH_CONSTANTS
#include "./vsm/common.inc.glsl"

#endif