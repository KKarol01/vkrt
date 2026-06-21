#pragma once

#if 0
namespace eng
{
namespace serialization
{

// clang-format off
// Thanks to this blog: https://www.scs.stanford.edu/~dm/blog/va-opt.html
// This code requires standard-conforming flag set for MSVC compiler
// Expand forces preprocessor to output argument, and rescan it, doing macro expansion again.
// It's nested to do it over 300 times to make sure everything gets expanded.
#define ENG_SERIALIZATION__EXPAND(...)  ENG_SERIALIZATION__EXPAND4(ENG_SERIALIZATION__EXPAND4(ENG_SERIALIZATION__EXPAND4(ENG_SERIALIZATION__EXPAND4(__VA_ARGS__))))
#define ENG_SERIALIZATION__EXPAND4(...) ENG_SERIALIZATION__EXPAND3(ENG_SERIALIZATION__EXPAND3(ENG_SERIALIZATION__EXPAND3(ENG_SERIALIZATION__EXPAND3(__VA_ARGS__))))
#define ENG_SERIALIZATION__EXPAND3(...) ENG_SERIALIZATION__EXPAND2(ENG_SERIALIZATION__EXPAND2(ENG_SERIALIZATION__EXPAND2(ENG_SERIALIZATION__EXPAND2(__VA_ARGS__))))
#define ENG_SERIALIZATION__EXPAND2(...) ENG_SERIALIZATION__EXPAND1(ENG_SERIALIZATION__EXPAND1(ENG_SERIALIZATION__EXPAND1(ENG_SERIALIZATION__EXPAND1(__VA_ARGS__))))
#define ENG_SERIALIZATION__EXPAND1(...) __VA_ARGS__
#define ENG_SERIALIZATION__PARENS ()
// Calls func once for each argument in the __VA_ARGS__: func(a1) func(a2) and so on
#define ENG_SERIALIZATION__FOR_EACH(func, ...) __VA_OPT__(ENG_SERIALIZATION__EXPAND(ENG_SERIALIZATION__FOR_EACH_HELPER(func, __VA_ARGS__)))
// HELPER and HELPER2 "call" each other in turns.
// This is because if HELPER expands to HELPER, proproccesor is still expanding HELPER (the replacing bit is set for HELPER)
// and if it sees during expansion the same token, it sets unavailable bit, wich is never ever cleared, so any futures expansions
// of HELPER will forever be precluded and will always produce just the text HELPER. Using HELPER2 uses another, different macro (with 2 at the end),
// so during replacement HELPER is not seen, and the unavailable never set, and subsequent calls to expand transform this ping-pong code
// to correctly expanded std::make_tuple() with comma-separated args without setting this trap bit.
#define ENG_SERIALIZATION__FOR_EACH_HELPER(func, Type, field, ...) func(Type, field) __VA_OPT__(, ENG_SERIALIZATION__FOR_EACH_HELPER2 ENG_SERIALIZATION__PARENS (func, Type, __VA_ARGS__))
#define ENG_SERIALIZATION__FOR_EACH_HELPER2() ENG_SERIALIZATION__FOR_EACH_HELPER
#define ENG_SERIALIZATION__FIELD(Type, field) StructField { &Type::field }
#define ENG_SERIALIZATION__FIELD_TUPLE(Type, ...) std::make_tuple(ENG_SERIALIZATION__FOR_EACH(ENG_SERIALIZATION__FIELD, Type, __VA_ARGS__))

// Specialize this function with ENG_SERIALIZATION_REGISTER_TYPE for a type that you want to be automatically serialized.
template <typename T> constexpr auto get_struct_fields() { static_assert(false, "Specialize this function with ENG_SERIALIZATION_REGISTER_TYPE."); }
#define ENG_SERIALIZATION_REGISTER_TYPE(Type) template<> constexpr auto get_struct_fields<Type>();
#define ENG_SERIALIZATION_REGISTER_TYPE_FIELDS(Type, ...) template<> constexpr auto get_struct_fields<Type>() { return ENG_SERIALIZATION__FIELD_TUPLE(Type, __VA_ARGS__); }
// clang-format on

} // namespace serialization
} // namespace eng
#endif