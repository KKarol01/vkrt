#ifndef IMGUI_COMMON_H
#define IMGUI_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.inc.glsl"
#endif

struct ImGuiVertex
{
    vec2 pos;
    vec2 uv;
    uint32_t color;
};

ENG_DECLARE_STORAGE_BUFFER(GPUImGuiVertexBuffer) {
    ENG_TYPE_UNSIZED(ImGuiVertex, vertices);
} ENG_DECLARE_BINDLESS(GPUImGuiVertexBuffer);


#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
    vec2 scale;
    vec2 translate;
    uint32_t vertex_buffer_index;
    uint32_t texture_index;
};
#endif

#define imgui_vertices storageBuffers_GPUImGuiVertexBuffer[vertex_buffer_index].vertices_us
#define imgui_texture  combinedImages_2d[texture_index]

#endif