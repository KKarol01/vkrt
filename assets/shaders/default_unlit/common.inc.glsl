#ifndef UNLIT_COMMON_H
#define UNLIT_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.glsli"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
    uint32_t engconstsb; // engine constants buffer index
    uint32_t imidb; // per-instance gpu resource ids buffer index

    // uint32_t indices_index;
    // uint32_t vertex_positions_index;
    // uint32_t vertex_attributes_index;
    // uint32_t transforms_index;
    // uint32_t constants_index;
    // uint32_t meshlet_instance_index;
    // uint32_t meshlet_ids_index;
    // uint32_t meshlet_bs_index;
    // uint32_t hiz_pyramid_index;
    // uint32_t hiz_debug_index;
};
#endif

#define engconsts          storageBuffers_GPUEngConstantsBuffer[engconstsb]
#define engvpos            storageBuffers_GPUVertexPositionsBuffer[engconsts.vposb].positions_us
#define get_trs(idx)       storageBuffers_GPUTransformsBuffer[engconsts.itrsb].transforms_us[idx]
#define get_id(idx)        storageBuffers_GPUMeshletIdsBuffer[imidb].ids_us[idx]

// #define meshlet_ids     storageBuffers_GPUMeshletIdsBuffer[meshlet_instance_index].ids_us
// #define culled_ids      storageBuffers_GPUPostCullIdsBuffer[meshlet_ids_index].ids_us
// #define meshlets_bs     storageBuffers_GPUMeshletBoundingSpheresBuffer[meshlet_bs_index].bounding_spheres_us
// #define transforms      storageBuffers_GPUTransformsBuffer[transforms_index].transforms_us
// #define hiz_pyramid     combinedImages_2d[hiz_pyramid_index]
// #define hiz_debug       combinedImages_2d[hiz_debug_index]
#endif