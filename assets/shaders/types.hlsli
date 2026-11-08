#ifndef ENG_TYPES_HLSLI
#define ENG_TYPES_HLSLI

// Scalar/vector/matrix aliases that resolve to HLSL builtins on the GPU and glm on the CPU,
// so structs declared with them have the same layout on both sides (dxc is run with -fvk-use-scalar-layout).

#ifdef __cplusplus
#include <cstdint>
#include <glm/glm.hpp>

#define ENG_INT int32_t
#define ENG_INT2 glm::ivec2
#define ENG_INT3 glm::ivec3
#define ENG_INT4 glm::ivec4

#define ENG_UINT uint32_t
#define ENG_UINT2 glm::uvec2
#define ENG_UINT3 glm::uvec3
#define ENG_UINT4 glm::uvec4

#define ENG_FLOAT float
#define ENG_FLOAT2 glm::vec2
#define ENG_FLOAT3 glm::vec3
#define ENG_FLOAT4 glm::vec4

#define ENG_MAT3x4 glm::mat3x4
#define ENG_MAT3 glm::mat3
#define ENG_MAT4 glm::mat4

#define ENG_INLINE inline

#define ENG_NAMESPACE_BEGIN namespace eng {
#define ENG_NAMESPACE_END }

#else
#define ENG_INT int
#define ENG_INT2 int2
#define ENG_INT3 int3
#define ENG_INT4 int4

#define ENG_UINT uint
#define ENG_UINT2 uint2
#define ENG_UINT3 uint3
#define ENG_UINT4 uint4

#define ENG_FLOAT float
#define ENG_FLOAT2 float2
#define ENG_FLOAT3 float3
#define ENG_FLOAT4 float4

#define ENG_MAT3x4 float3x4
#define ENG_MAT3 float3x3
#define ENG_MAT4 float4x4

#define ENG_INLINE

#define ENG_NAMESPACE_BEGIN
#define ENG_NAMESPACE_END

#endif

#endif
