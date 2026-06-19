#pragma once

#include <glm/fwd.hpp>
#include <concepts>
#include <eng/common/handle.hpp>
#include <eng/common/scalar_types.hpp>
#include <eng/common/flags.hpp>
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
using f32_2 = glm::f32vec2;
using f32_4 = glm::f32vec4;
using i32_3 = glm::i32vec3;
using u32_2 = glm::u32vec2;
using u32_3 = glm::u32vec3;
using StringHash = u64;

} // namespace eng

namespace std
{
template <typename T, typename Storage> struct hash<eng::TypedId<T, Storage>>
{
    size_t operator()(const eng::TypedId<T, Storage>& t) const { return *t; }
};
template <typename T> struct hash<eng::Range_T<T>>
{
    size_t operator()(const eng::Range_T<T>& t) const { return ENG_HASH(t.offset, t.size); }
};
} // namespace std