#pragma once

#include <cstdint>
#include <cassert>
#include <bit>

namespace eng
{

// checks power of two
inline bool is_pow2(size_t a) { return (a & (a - 1)) == 0; }

// aligns up
inline size_t align_up(size_t a, size_t b)
{
    const auto r = a % b;
    return r > 0 ? a + (b - r) : a;
}

// aligns a to the nearest multiple of any power of two.
inline size_t align_up2(size_t a, size_t b)
{
    assert(is_pow2(b));
    return (a + b - 1) & ~(b - 1);
}

// aligns a to the nearest multiple of any power of two.
inline size_t align_down2(size_t a, size_t b)
{
    assert(is_pow2(b));
    return a & ~(b - 1);
}

// aligns up a pointer
inline void* align_up2(void* ptr, size_t b)
{
    assert(ptr != nullptr && is_pow2(b));
    return reinterpret_cast<void*>(align_up2(reinterpret_cast<std::uintptr_t>(ptr), b));
}

inline size_t next_power_of_2(size_t a) { return std::bit_ceil(a); }

} // namespace eng