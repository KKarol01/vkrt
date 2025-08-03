#ifndef IMGUI_COMMON_H
#define IMGUI_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.inc.glsl"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
    uint32_t vertex_buffer_index;
    uint32_t texture_index;
};
#endif

#endif