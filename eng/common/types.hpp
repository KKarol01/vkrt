#pragma once
#include <eng/common/hash.hpp>
#include <glm/fwd.hpp>
#include <cstdint>

namespace eng
{

template <class... Ts> struct Visitor : Ts...
{
    using Ts::operator()...;
};

template <typename T, std::integral Storage> struct TypedId
{
    using StorageType = Storage;
    constexpr TypedId() = default;
    constexpr explicit TypedId(StorageType handle) : handle{ handle } {}
    constexpr StorageType operator*() const { return handle; }
    constexpr bool operator==(const TypedId& a) const { return (bool)*this && a && handle == a.handle; }
    constexpr auto operator<=>(const TypedId& a) const { return handle <=> a.handle; }
    constexpr explicit operator bool() const { return handle != ~StorageType{}; }
    StorageType handle{ ~StorageType{} };
};

template <typename Storage, Storage INDEX_BITS> struct VersionedIndex;

// todo: maybe add configurable num of bits for index
template <uint32_t INDEX_BITS>
struct VersionedIndex<uint32_t, INDEX_BITS> : public TypedId<VersionedIndex<uint32_t, INDEX_BITS>, uint32_t>
{
    static_assert(INDEX_BITS <= 32);
    inline static constexpr StorageType INDEX_MASK = (StorageType{ 1u } << INDEX_BITS) - 1;
    using TypedId::TypedId;
    VersionedIndex(StorageType index, StorageType version) : TypedId((version << INDEX_BITS) | (index & INDEX_MASK)) {}
    StorageType get_index() const { return handle & INDEX_MASK; }
    StorageType get_version() const { return handle >> INDEX_BITS; }
};

template <typename Storage = size_t> struct Range_T
{
    auto operator<=>(const Range_T&) const = default;
    Storage offset{ ~0u };
    Storage size{};
};

using Range32u = Range_T<uint32_t>;
using Range64u = Range_T<uint64_t>;
using Range3D32i = Range_T<glm::i32vec3>;
using Range3D32u = Range_T<glm::u32vec3>;
using Range3D64i = Range_T<glm::i64vec3>;
using Vec3i32 = glm::i32vec3;
using Vec3u32 = glm::u32vec3;
using Color4f = glm::f32vec4;

} // namespace eng

ENG_DEFINE_STD_HASH(eng::Range32u, eng::hash::combine_fnv1a(t.offset, t.size));
ENG_DEFINE_STD_HASH(eng::Range64u, eng::hash::combine_fnv1a(t.offset, t.size));