#pragma once

#include <cstdint>

struct HashCombine
{
    template <typename... Args> static uint64_t hash(const Args&... args)
    {
        uint64_t seed{};
        (..., ([&seed](const auto& v) {
             std::hash<typename std::remove_cvref<decltype(v)>::type> hasher;
             seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
         })(args));
        return seed;
    }
};