#version 460

#include "./bindless_structures.glsli"

layout(scalar, push_constant) uniform PushConstants
{
	uint32_t engconstsb;
};

#define engconsts          storageBuffers_GPUEngConstantsBuffer[engconstsb]
#define engvpos            storageBuffers_GPUVertexPositionsBuffer[engconsts.vposb].positions_us

void main()
{
	vec4 pos = vec4(engvpos[gl_VertexIndex].xyz, 1.0);
	pos = engconsts.proj_view * pos;
    gl_Position = pos;
}