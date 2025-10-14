#pragma once
#include <eng/common/hash.hpp>
#include <glm/fwd.hpp>

template <class... Ts> struct Visitor : Ts...
{
    using Ts::operator()...;
};

template <typename Storage = size_t> struct Range_T
{
    auto operator<=>(const Range_T&) const = default;
    Storage offset{};
    Storage size{};
};

using Range32u = Range_T<uint32_t>;
using Range64u = Range_T<uint64_t>;
using Range = Range64u;
using Range3D32i = Range_T<glm::i32vec3>;
using Range3D32u = Range_T<glm::u32vec3>;
using Range3D64i = Range_T<glm::i64vec3>;
using Vec3i32 = glm::i32vec3;
using Vec3u32 = glm::u32vec3;

ENG_DEFINE_STD_HASH(Range32u, eng::hash::combine_fnv1a(t.offset, t.size));
ENG_DEFINE_STD_HASH(Range64u, eng::hash::combine_fnv1a(t.offset, t.size));