#pragma once
#include <eng/common/hash.hpp>

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

using Range32 = Range_T<uint32_t>;
using Range64 = Range_T<uint64_t>;
using Range = Range64;

DEFINE_STD_HASH(Range32, eng::hash::combine_fnv1a(t.offset, t.size));
DEFINE_STD_HASH(Range64, eng::hash::combine_fnv1a(t.offset, t.size));