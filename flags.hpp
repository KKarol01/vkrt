#pragma once

#include <type_traits>

template <typename T> struct Flags {
    using U = typename std::underlying_type_t<T>;

    constexpr Flags() = default;
    constexpr Flags(T t) noexcept : flags(static_cast<U>(t)) {}
    constexpr Flags(U t) noexcept : flags(t) {}

    friend constexpr Flags<T> operator|(Flags<T> a, T b) { return Flags<T>{ a.flags | static_cast<U>(b) }; }
    friend constexpr Flags<T> operator&(Flags<T> a, T b) { return Flags<T>{ a.flags & static_cast<U>(b) }; }
    constexpr Flags<T>& operator|=(T f) {
        flags = flags | static_cast<U>(f);
        return *this;
    }
    constexpr Flags<T>& operator&=(T f) {
        flags = flags & static_cast<U>(f);
        return *this;
    }
    constexpr Flags<T>& operator^=(T f) {
        flags = flags ^ static_cast<U>(f);
        return *this;
    }

    constexpr operator bool() const { return flags > 0; }

    U flags{ 0 };
};