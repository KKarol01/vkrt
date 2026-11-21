#pragma once

#include <cstdint>

namespace eng
{
inline namespace scalar_types
{
using i8 = int8_t;
using u8 = uint8_t;
using i16 = int16_t;
using u16 = uint16_t;
using i32 = int32_t;
using u32 = uint32_t;
using i64 = int64_t;
using u64 = uint64_t;
using isize = std::make_signed_t<size_t>;
using usize = size_t;
using f32 = float;
using f64 = double;
static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);
} // namespace scalar_types
} // namespace eng