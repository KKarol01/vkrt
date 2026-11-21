#ifndef ENG_BINDLESS_HLSLI
#define ENG_BINDLESS_HLSLI

#include "assets/shaders/types.hlsli"

#define ENG_BINDLESS_STORAGE_BUFFER_BINDING 0
#define ENG_BINDLESS_STORAGE_IMAGE_BINDING 1
 #define ENG_BINDLESS_COMBINED_IMAGE_BINDING 2
#define ENG_BINDLESS_SAMPLED_IMAGE_BINDING 3
#define ENG_BINDLESS_SAMPLER_BINDING 4
#define ENG_BINDLESS_ACCELERATION_STRUCT_BINDING 5

#define ENG_SAMPLER_LINEAR 0
#define ENG_SAMPLER_LINEAR_CLAMP 1
#define ENG_SAMPLER_NEAREST 2
#define ENG_SAMPLER_HIZ 3
#define ENG_SAMPLER_COUNT 4

#define ENG_INVALID_INDEX ~0u

#ifdef __cplusplus

ENG_NAMESPACE_BEGIN
// Refs are constructible from a plain bindless index (see get_bindless_*_index() in renderer.hpp).
template <typename T> struct BufferRef
{
    constexpr BufferRef() = default;
    constexpr BufferRef(ENG_UINT index) : index(index) {}
    ENG_UINT index{ ENG_INVALID_INDEX };
};
template <typename T> struct Texture2DRef
{
    constexpr Texture2DRef() = default;
    constexpr Texture2DRef(ENG_UINT index) : index(index) {}
    ENG_UINT index{ ENG_INVALID_INDEX };
};
template <typename T> struct RWTexture2DRef
{
    constexpr RWTexture2DRef() = default;
    constexpr RWTexture2DRef(ENG_UINT index) : index(index) {}
    ENG_UINT index{ ENG_INVALID_INDEX };
};
struct TlasRef
{
    constexpr TlasRef() = default;
    constexpr TlasRef(ENG_UINT index) : index(index) {}
    ENG_UINT index{ ENG_INVALID_INDEX };
};
ENG_NAMESPACE_END

#else

[[vk::binding(ENG_BINDLESS_STORAGE_BUFFER_BINDING, 0)]] RWByteAddressBuffer gRWBuffers[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float> gRWTextures2Dfloat[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float2> gRWTextures2Dfloat2[];
[[vk::binding(ENG_BINDLESS_STORAGE_IMAGE_BINDING, 0)]] RWTexture2D<float4> gRWTextures2Dfloat4[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float> gTextures2Dfloat[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float2> gTextures2Dfloat2[];
[[vk::binding(ENG_BINDLESS_SAMPLED_IMAGE_BINDING, 0)]] Texture2D<float4> gTextures2Dfloat4[];
[[vk::binding(ENG_BINDLESS_SAMPLER_BINDING, 0)]] SamplerState gSamplerStates[];
[[vk::binding(ENG_BINDLESS_ACCELERATION_STRUCT_BINDING, 0)]] RaytracingAccelerationStructure gTLASs[];

#define gSamplerLinear gSamplerStates[ENG_SAMPLER_LINEAR]
#define gSamplerLinearClamp gSamplerStates[ENG_SAMPLER_LINEAR_CLAMP]
#define gSamplerNearest gSamplerStates[ENG_SAMPLER_NEAREST]
#define gSamplerHiz gSamplerStates[ENG_SAMPLER_HIZ]

template <typename T> struct BufferRef
{
    T load() { return load(0); }
    T load(uint i) { return gRWBuffers[index].template Load<T>(i * sizeof(T)); }
    void store(uint i, T value) { gRWBuffers[index].template Store<T>(i * sizeof(T), value); }
    T load_nonuniform(uint i) { return gRWBuffers[NonUniformResourceIndex(index)].template Load<T>(i * sizeof(T)); }
    RWByteAddressBuffer get() { return gRWBuffers[index]; }
    bool is_valid() { return index != ENG_INVALID_INDEX; }
    uint index;
};

template <typename T> struct Texture2DRef
{
    uint index;
};

template <typename T> struct RWTexture2DRef
{
    uint index;
};

#define ENG_DEFINE_TEXTURE_REFS(T)                                                                                     \
    template <> struct Texture2DRef<T>                                                                                 \
    {                                                                                                                  \
        Texture2D<T> get() { return gTextures2D##T[index]; }                                                           \
        Texture2D<T> get_nonuniform() { return gTextures2D##T[NonUniformResourceIndex(index)]; }                       \
        bool is_valid() { return index != ENG_INVALID_INDEX; }                                                         \
        uint index;                                                                                                    \
    };                                                                                                                 \
    template <> struct RWTexture2DRef<T>                                                                               \
    {                                                                                                                  \
        RWTexture2D<T> get() { return gRWTextures2D##T[index]; }                                                       \
        RWTexture2D<T> get_nonuniform() { return gRWTextures2D##T[NonUniformResourceIndex(index)]; }                   \
        bool is_valid() { return index != ENG_INVALID_INDEX; }                                                         \
        uint index;                                                                                                    \
    };

ENG_DEFINE_TEXTURE_REFS(float)
ENG_DEFINE_TEXTURE_REFS(float2)
ENG_DEFINE_TEXTURE_REFS(float4)
#undef ENG_DEFINE_TEXTURE_REFS

struct TlasRef
{
    RaytracingAccelerationStructure get() { return gTLASs[index]; }
    bool is_valid() { return index != ENG_INVALID_INDEX; }
    uint index;
};

#define ENG_PUSH_CONSTANTS(T) [[vk::push_constant]] T pc;

#endif

#endif
