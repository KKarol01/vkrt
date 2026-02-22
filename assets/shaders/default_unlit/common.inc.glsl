#ifndef UNLIT_COMMON_H
#define UNLIT_COMMON_H

#ifndef NO_BINDLESS_STRUCTS_INCLUDE
#include "./bindless_structures.glsli"
#endif

#ifndef NO_PUSH_CONSTANTS
layout(scalar, push_constant) uniform PushConstants
{
	uint32_t GPUEngConstantsBufferIndex;
	uint32_t GPUVertexPositionsBufferIndex;
	// uint32_t imidb;
	// uint32_t GPUFWDPLightGridsBufferIndex;
	// uint32_t GPUFWDPLightListsBufferIndex;
};
#endif

#endif