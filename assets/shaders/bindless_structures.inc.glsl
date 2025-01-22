#define BINDLESS_STORAGE_BUFFER_BINDING 0
#define BINDLESS_STORAGE_IMAGE_BINDING 1
#define BINDLESS_COMBINED_IMAGE_BINDING 2
#define ENG_TYPE_INT int32_t
#define ENG_TYPE_UINT uint32_t
#define ENG_TYPE_FLOAT float


#ifdef VULKAN

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#define ENG_TYPE_IVEC2 ivec2
#define ENG_TYPE_IVEC3 ivec3
#define ENG_TYPE_IVEC4 ivec4

#define ENG_TYPE_UVEC2 uvec2
#define ENG_TYPE_UVEC3 uvec3
#define ENG_TYPE_UVEC4 uvec4

#define ENG_TYPE_VEC2 vec2
#define ENG_TYPE_VEC3 vec3
#define ENG_TYPE_VEC4 vec4

#define ENG_TYPE_MAT3 mat3
#define ENG_TYPE_MAT4 mat4

#define BINDLESS_GET_LAYOUT_NAME(Name) Name##BindlessArray
#define BINDLESS_GET_LAYOUT_NAME_IMAGE(Dim, Format) StorageImages##BindlessArray##Dim##Format
#define BINDLESS_GET_LAYOUT_NAME_COMBINED(Dim) CombinedImages##BindlessArray##Dim

#define BINDLESS_DECLARE_STORAGE_BUFFER(access, name, data) \
	layout(scalar, set=0, binding=BINDLESS_STORAGE_BUFFER_BINDING) access buffer name data BINDLESS_GET_LAYOUT_NAME(name)[]

#define BINDLESS_DECLARE_IMAGE(access, format, dim) \
	layout(set=0, binding=BINDLESS_STORAGE_IMAGE_BINDING, format) uniform image##dim BINDLESS_GET_LAYOUT_NAME_IMAGE(dim, format)[]

#define BINDLESS_DECLARE_COMBINED_IMAGE(dim) \
	layout(set = 0, binding = BINDLESS_COMBINED_IMAGE_BINDING) uniform sampler##dim BINDLESS_GET_LAYOUT_NAME_COMBINED(dim)[]

#define GetResource(name, index) BINDLESS_GET_LAYOUT_NAME(name)[index]
#define GetImage(index, dim, format) BINDLESS_GET_LAYOUT_NAME_IMAGE(dim, format)[index]
#define GetCombined(index, dim) BINDLESS_GET_LAYOUT_NAME_COMBINED(dim)[index]

#elif __cplusplus
#define ENG_TYPE_IVEC2 glm::ivec2
#define ENG_TYPE_IVEC3 glm::ivec3
#define ENG_TYPE_IVEC4 glm::ivec4

#define ENG_TYPE_UVEC2 glm::uvec2
#define ENG_TYPE_UVEC3 glm::uvec3
#define ENG_TYPE_UVEC4 glm::uvec4

#define ENG_TYPE_VEC2 glm::vec2
#define ENG_TYPE_VEC3 glm::vec3
#define ENG_TYPE_VEC4 glm::vec4

#define ENG_TYPE_MAT3 glm::mat3
#define ENG_TYPE_MAT4 glm::mat4
#endif

struct GPUConstants {
	ENG_TYPE_MAT4 view;
	ENG_TYPE_MAT4 proj;
	ENG_TYPE_MAT4 inv_view;
	ENG_TYPE_MAT4 inv_proj;
	ENG_TYPE_MAT3 rand_mat;
};

#ifdef VULKAN
BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUConstantsBuffer, {
	GPUConstants constants;
});

BINDLESS_DECLARE_STORAGE_BUFFER(readonly, GPUVertexPositionsBuffer, {
	ENG_TYPE_VEC3 at[];
});

BINDLESS_DECLARE_IMAGE(, rgba8, 2D);
BINDLESS_DECLARE_COMBINED_IMAGE(2D);
#endif