#pragma once

#include <type_traits>

#define ENABLE_FLAGS_OPERATORS(Type)                                                                                   \
    constexpr Flags<Type> operator|(const Type& a, const Type& b) noexcept {                                           \
        return Flags<Type>{ a } | Flags<Type>{ b };                                                                    \
    }

template <typename T> struct Flags {
    using U = typename std::underlying_type_t<T>;

    constexpr Flags() = default;
    constexpr Flags(T t) noexcept : flags(static_cast<U>(t)) {}
    constexpr Flags(U t) noexcept : flags(t) {}

    constexpr Flags<T> operator|(Flags<T> a) noexcept { return Flags<T>{ flags | a.flags }; }
    constexpr Flags<T> operator&(Flags<T> a) noexcept { return Flags<T>{ a.flags & flags }; }
    constexpr Flags<T> operator~() noexcept { return Flags<T>{ ~flags }; }
    constexpr Flags<T>& operator|=(Flags<T> f) noexcept {
        flags = flags | f.flags;
        return *this;
    }
    constexpr Flags<T>& operator&=(Flags<T> f) noexcept {
        flags = flags & f.flags;
        return *this;
    }
    constexpr Flags<T>& operator^=(Flags<T> f) noexcept {
        flags = flags ^ f.flags;
        return *this;
    }

    constexpr operator bool() const noexcept { return flags != 0; }
    constexpr explicit operator U() const noexcept { return flags; }

    constexpr void set(Flags<T> f) noexcept { *this |= f; }
    constexpr bool test(Flags<T> f) const noexcept { return (flags & f.flags) == f.flags; }
    constexpr bool test_clear(Flags<T> f) {
        const auto result = test(f);
        clear(f);
        return result;
    }
    constexpr void clear(Flags<T> f) noexcept { *this &= ~f; }

    U flags{ 0 };
};