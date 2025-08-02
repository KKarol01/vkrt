#pragma once

// aligns a to the nearest multiple of any power of two
template <std::integral T> static T align_up2(T a, T b) { return (a + b - 1) & -b; }