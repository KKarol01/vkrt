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

#define engconsts          gsb_GPUEngConstantsBuffer[engconstsb]
#define engvpos            gsb_GPUVertexPositionsBuffer[engconsts.vposb].positions_us
#define engvattrs          gsb_GPUVertexAttributesBuffer[engconsts.vatrb].attributes_us
#define get_trs(idx)       gsb_GPUTransformsBuffer[engconsts.itrsb].transforms_us[idx]
#define get_id(idx)        gsb_GPUMeshletIdsBuffer[imidb].ids_us[idx]
#define get_mat(idx)       gsb_GPUMaterialsBuffer[engconsts.rmatb].materials_us[idx]

#endif