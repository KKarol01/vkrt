#pragma once

#include <type_traits>
#include <compare>

#define ENG_ENABLE_FLAGS_OPERATORS(Type)                                                                               \
    constexpr Flags<Type> operator|(const Type& a, const Type& b) noexcept                                             \
    {                                                                                                                  \
        return Flags<Type>{ a } | Flags<Type>{ b };                                                                    \
    }

template <typename T> struct Flags
{
    using U = typename std::underlying_type_t<T>;

    constexpr Flags() = default;
    constexpr Flags(T t) noexcept : flags(static_cast<U>(t)) {}
    constexpr Flags(U t) noexcept : flags(t) {}

    constexpr auto operator<=>(const Flags<T>& a) const noexcept = default;
    constexpr Flags<T> operator|(Flags<T> a) const noexcept { return Flags<T>{ flags | a.flags }; }
    constexpr Flags<T> operator&(Flags<T> a) const noexcept { return Flags<T>{ a.flags & flags }; }
    constexpr Flags<T> operator~() const noexcept { return Flags<T>{ ~flags }; }
    constexpr Flags<T>& operator|=(Flags<T> f) noexcept
    {
        flags = flags | f.flags;
        return *this;
    }
    constexpr Flags<T>& operator&=(Flags<T> f) noexcept
    {
        flags = flags & f.flags;
        return *this;
    }
    constexpr Flags<T>& operator^=(Flags<T> f) noexcept
    {
        flags = flags ^ f.flags;
        return *this;
    }

    constexpr explicit operator bool() const noexcept { return flags != U{}; }
    constexpr explicit operator U() const noexcept { return flags; }

    constexpr bool empty() const { return flags == U{}; }
    constexpr void set(Flags<T> f) noexcept { *this |= f; }
    constexpr bool test(Flags<T> f) const noexcept { return (flags & f.flags) == f.flags; }
    constexpr bool test_clear(Flags<T> f)
    {
        const auto result = test(f);
        clear(f);
        return result;
    }
    constexpr void clear(Flags<T> f) noexcept { *this &= ~f; }
    constexpr void clear() noexcept { flags = U{}; }

    U flags{ 0 };
};