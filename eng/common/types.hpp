#pragma once
#include <eng/common/hash.hpp>

template <class... Ts> struct Visitor : Ts...
{
    using Ts::operator()...;
};

struct Range
{
    auto operator==(const Range& o) const { return offset == o.offset && size == o.size; }
    size_t offset{};
    size_t size{};
};

DEFINE_STD_HASH(Range, eng::hash::combine_fnv1a(t.offset, t.size));