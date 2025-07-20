#ifndef CULLING_COMMON_H
#define CULLING_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.inc.glsl"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
    uint32_t constants_index;
    uint32_t ids_index;
    uint32_t post_cull_ids_index;
    uint32_t bs_index;
    uint32_t transforms_index;
    uint32_t indirect_commands_index;
    uint32_t hiz_source_index;
    uint32_t hiz_dest_index;
    uint32_t hiz_width;
    uint32_t hiz_height;
};
#endif

#define instance_ids                storageBuffers_GPUMeshletIdsBuffer[ids_index]
#define post_cull_instance_ids      storageBuffers_GPUPostCullIdsBuffer[post_cull_ids_index].ids_us
#define instance_bs                 storageBuffers_GPUMeshletBoundingSpheresBuffer[bs_index].bounding_spheres_us
#define indirect_cmds               storageBuffers_GPUDrawIndirectCommandsBuffer[indirect_commands_index]
#define hiz_source                  combinedImages_2d[hiz_source_index]
#define hiz_dest                    storageImages_2dr16f[hiz_dest_index]
#define hiz_debug                   storageImages_2drgba32f[hiz_dest_index]

#endif