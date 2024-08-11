layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer CommonValues {
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation; // halton 4-frame jitter
};

struct MeshData {
    uint32_t color_texture;
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer MeshDatas {
    MeshData at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerMeshInstanceMeshIds {
    uint32_t at[];
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerMeshInstanceTransforms {
    mat4x3 at[];
};