#pragma once
#include <eng/common/hash.hpp>
#include <glm/fwd.hpp>
#include <cstdint>
#include <concepts>

namespace eng
{

template <class... Ts> struct Visitor : Ts...
{
    using Ts::operator()...;
};

/**
 Strongly typed integral type. Introduced to avoid bugs when dealing with indices/ids/handles
 from different systems, storages, etc.

 Initially all bits are set to 1, and that is an invalid state.
 In it, the id always compares to false.

 Dereference operator returns the underlying value.
 */
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

template <typename Storage = size_t> struct Range_T
{
    auto operator<=>(const Range_T&) const = default;
    Storage offset{ ~Storage{} };
    Storage size{};
};

using Range32u = Range_T<uint32_t>;
using Range64u = Range_T<uint64_t>;
using Range3D32i = Range_T<glm::i32vec3>;
using Range3D32u = Range_T<glm::u32vec3>;
using Range3D64i = Range_T<glm::i64vec3>;
using Vec2f = glm::vec2;
using Vec3i32 = glm::i32vec3;
using Vec3u32 = glm::u32vec3;
using Color4f = glm::f32vec4;
using StringHash = uint64_t;

} // namespace eng

namespace std
{
template <typename T, typename Storage> struct hash<eng::TypedId<T, Storage>>
{
    size_t operator()(const eng::TypedId<T, Storage>& t) const { return *t; }
};
} // namespace std

ENG_DEFINE_STD_HASH(eng::Range32u, ENG_HASH(t.offset, t.size));
ENG_DEFINE_STD_HASH(eng::Range64u, ENG_HASH(t.offset, t.size));