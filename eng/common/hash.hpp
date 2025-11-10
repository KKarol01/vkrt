#pragma once

#include <cstdint>
#include <bit>
#include <limits>
#include <cassert>

namespace eng
{
namespace hash
{
uint64_t combine_fnv1a(const auto&... args)
{
    static constexpr uint64_t offset_basis = 0xcbf29ce484222325ull;
    static constexpr uint64_t prime = 0x100000001b3ull;
    auto hash = offset_basis;
    (..., [&hash](const auto& v) {
        const uint64_t stdhash = std::hash<std::remove_cvref_t<decltype(v)>>{}(v);
        for(auto i = 0; i < 8; ++i)
        {
            hash = hash ^ reinterpret_cast<const uint8_t*>(&stdhash)[i];
            hash = hash * prime;
        }
    }(args));
    return hash;
}
} // namespace hash
} // namespace eng

#define ENG_DEFINE_STD_HASH(type, code)                                                                                \
    namespace std                                                                                                      \
    {                                                                                                                  \
    template <> struct hash<type>                                                                                      \
    {                                                                                                                  \
        size_t operator()(const type& t) const { return code; }                                                        \
    };                                                                                                                 \
    }
