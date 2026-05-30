#pragma once
#include <cstdint>
#include <bit>
#include <array>
#include <type_traits>
#include <eng/common/scalar_types.hpp>

namespace eng
{
namespace hash
{

inline constexpr u64 FNV1A_BASIS = 0xcbf29ce484222325ull;
inline constexpr u64 FNV1A_PRIME = 0x100000001b3ull;

template <typename T>
concept is_range = requires(T t) { std::span{ t }; };

inline constexpr u64 fnv1a_bytes(u64 hash, const u8* const bytes, usize size)
{
    for(auto i = 0ull; i < size; ++i)
    {
        hash = hash ^ bytes[i];
        hash = hash * FNV1A_PRIME;
    }
    return hash;
}

template <typename T> inline constexpr u64 fnv1a_value(u64 hash, const T& t)
{
    using U = std::remove_cvref_t<T>;

    if constexpr(std::integral<U> || std::is_enum_v<U>)
    {
        auto bytes = std::bit_cast<std::array<u8, sizeof(T)>>(t);
        return fnv1a_bytes(hash, bytes.data(), bytes.size());
    }
    else if constexpr(std::convertible_to<U, std::string_view>)
    {
        std::string_view sv = t;
        return fnv1a_bytes(hash, reinterpret_cast<const u8*>(sv.data()), sv.size());
    }
    else if constexpr(is_range<U>)
    {
        auto s = std::span{ t };
        if constexpr(sizeof(typename decltype(s)::value_type) == 1)
        {
            return fnv1a_bytes(hash, reinterpret_cast<const u8*>(s.data()), s.size());
        }
        else
        {
            for(const auto& e : s)
            {
                hash = fnv1a_value(hash, e);
            }
            return hash;
        }
    }
    else
    {
        const usize standard_hash = std::hash<U>{}(t);
        return fnv1a_value(hash, standard_hash);
    }
}

template <typename... Args> inline constexpr u64 fnv1a_list(const auto&... args)
{
    u64 hash = FNV1A_BASIS;
    ((hash = fnv1a_value(hash, args)), ...);
    return hash;
}

struct PairHash
{
    template <typename T1, typename T2> usize operator()(const std::pair<T1, T2>& p) const
    {
        return eng::hash::fnv1a_list(p.first, p.second);
    }
};

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

#define ENG_HASH(...) eng::hash::fnv1a_list(__VA_ARGS__)