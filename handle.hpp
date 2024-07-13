#pragma once

#include <cstdint>
#include <compare>

template <typename T, typename Storage = std::uint32_t> struct Handle {
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : _handle{ handle } {}
    constexpr Storage operator*() const { return _handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    using StorageType = Storage;
    Storage _handle{ ~Storage{ 0 } };
};