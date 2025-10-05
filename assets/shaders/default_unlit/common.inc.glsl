#ifndef UNLIT_COMMON_H
#define UNLIT_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.glsli"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
    uint32_t engconstsb;
    uint32_t imidb;
};
#endif

#define engconsts          storageBuffers_GPUEngConstantsBuffer[engconstsb]
#define engvpos            storageBuffers_GPUVertexPositionsBuffer[engconsts.vposb].positions_us
#define engvattrs          storageBuffers_GPUVertexAttributesBuffer[engconsts.vatrb].attributes_us
#define get_trs(idx)       storageBuffers_GPUTransformsBuffer[engconsts.itrsb].transforms_us[idx]
#define get_id(idx)        storageBuffers_GPUMeshletIdsBuffer[imidb].ids_us[idx]
#define get_mat(idx)       storageBuffers_GPUMaterialsBuffer[engconsts.rmatb].materials_us[idx]

#endif