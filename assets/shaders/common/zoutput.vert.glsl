#version 460

#include "./bindless_structures.glsli"

layout(scalar, push_constant) uniform PushConstants
{
	uint32_t engconstsb;
    uint32_t imidb;
};

#define engvpos            storageBuffers_GPUVertexPositionsBuffer[engconsts.vposb].positions_us
#define engconsts          storageBuffers_GPUEngConstantsBuffer[engconstsb]
#define get_trs(idx)       storageBuffers_GPUTransformsBuffer[engconsts.itrsb].transforms_us[idx]
#define get_id(idx)        storageBuffers_GPUMeshletIdsBuffer[imidb].ids_us[idx]

void main()
{
	GPUInstanceId id = get_id(gl_InstanceIndex);
	vec4 pos = vec4(engvpos[gl_VertexIndex].xyz, 1.0);
	pos = get_trs(id.instidx) * pos;
	pos = engconsts.proj_view * pos;

    gl_Position = pos;
}