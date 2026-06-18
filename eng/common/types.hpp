#pragma once
#include <glm/fwd.hpp>
#include <concepts>
#include <eng/common/scalar_types.hpp>
#include <eng/common/hash.hpp>

namespace eng
{

inline constexpr usize KiB = 1024;
inline constexpr usize MiB = 1024 * 1024;
inline constexpr usize GiB = 1024 * 1024 * 1024;

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
    inline static constexpr StorageType INVALID_VALUE = ~StorageType{};
    constexpr TypedId() = default;
    constexpr explicit TypedId(StorageType handle) : handle{ handle } {}
    constexpr StorageType& operator*() { return handle; }
    constexpr const StorageType& operator*() const { return handle; }
    constexpr bool operator==(const TypedId& a) const { return (bool)*this && (bool)a && handle == a.handle; }
    constexpr auto operator<=>(const TypedId& a) const { return handle <=> a.handle; }
    constexpr explicit operator bool() const { return handle != INVALID_VALUE; }
    StorageType handle{ INVALID_VALUE };
};

template <typename Storage = usize> struct Range_T
{
    auto operator<=>(const Range_T&) const = default;
    Storage offset{};
    Storage size{};
};

using Range32u = Range_T<u32>;
using Range64u = Range_T<u64>;
using Range3D32i = Range_T<glm::i32vec3>;
using Range3D32u = Range_T<glm::u32vec3>;
using Range3D64i = Range_T<glm::i64vec3>;
using f32_2 = glm::vec2;
using i32_3 = glm::i32vec3;
using u32_2 = glm::u32vec2;
using u32_3 = glm::u32vec3;
using Color4f = glm::f32vec4;
using StringHash = u64;

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