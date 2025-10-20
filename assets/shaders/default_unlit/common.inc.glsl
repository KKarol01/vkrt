#ifndef UNLIT_COMMON_H
#define UNLIT_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.glsli"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
    uint32_t GPUEngConstantsBufferIndex;
    uint32_t imidb;
};
#endif

#define engvpos            gsb_GPUVertexPositionsBuffer[get_buf(GPUEngConstant).vposb].positions_us
#define engvattrs          gsb_GPUVertexAttributesBuffer[get_buf(GPUEngConstant).vatrb].attributes_us
#define get_trs(idx)       gsb_GPUTransformsBuffer[get_buf(GPUEngConstant).itrsb].transforms_us[idx]
#define get_id(idx)        gsb_GPUMeshletIdsBuffer[imidb].ids_us[idx]
#define get_mat(idx)       gsb_GPUMaterialsBuffer[get_buf(GPUEngConstant).rmatb].materials_us[idx]

#endif