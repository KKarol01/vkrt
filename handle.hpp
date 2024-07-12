#pragma once

#include <cstdint>

template <typename T, typename Storage = std::uint32_t> struct Handle {
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : _handle{ handle } {}
    constexpr Storage operator*() const { return _handle; }
    using StorageType = Storage;
    Storage _handle{ ~Storage{ 0 } };
};