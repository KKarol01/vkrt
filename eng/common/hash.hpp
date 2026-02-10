#pragma once

#include <cstdint>
#include <bit>
#include <array>
#include <type_traits>

namespace eng
{
namespace hash
{

inline constexpr uint64_t fnv1a_bytes(uint64_t hash, const uint8_t* const bytes, size_t size)
{
    static constexpr uint64_t prime = 0x100000001b3ull;
    for(auto i = 0ull; i < size; ++i)
    {
        hash = hash ^ bytes[i];
        hash = hash * prime;
    }
    return hash;
}

template <typename T> inline constexpr uint64_t fnv1a_value(uint64_t hash, const T& t)
{
    if constexpr(std::is_trivially_copyable_v<T>)
    {
        if(std::is_constant_evaluated())
        {
            auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(t);
            return fnv1a_bytes(hash, bytes.data(), bytes.size());
        }
    }
    auto thash = std::hash<T>{}(t);
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(thash)>>(thash);
    return fnv1a_bytes(hash, bytes.data(), bytes.size());
}

inline constexpr uint64_t combine_fnv1a(const auto&... args)
{
    static constexpr uint64_t offset_basis = 0xcbf29ce484222325ull;
    uint64_t hash = offset_basis;
    ((hash = fnv1a_value(hash, args)), ...);
    return hash;
}

inline constexpr uint64_t combine_fnv1a(const char* str)
{
    static constexpr uint64_t offset_basis = 0xcbf29ce484222325ull;
    static constexpr uint64_t prime = 0x100000001b3ull;
    uint64_t hash = offset_basis;
    while(*str)
    {
        hash ^= static_cast<unsigned char>(*str);
        hash *= prime;
        ++str;
    }
    return hash;
}

} // namespace hash
} // namespace eng

#define ENG_HASH(cstring) ::eng::hash::combine_fnv1a(cstring)

#define ENG_DEFINE_STD_HASH(type, code)                                                                                \
    namespace std                                                                                                      \
    {                                                                                                                  \
    template <> struct hash<type>                                                                                      \
    {                                                                                                                  \
        size_t operator()(const type& t) const { return code; }                                                        \
    };                                                                                                                 \
    }
