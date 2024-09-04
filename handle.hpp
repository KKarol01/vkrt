#pragma once

#include <cstdint>
#include <compare>

template <typename T, typename Storage = uint32_t> struct Handle;

template <typename T, typename Storage = uint32_t> struct HandleDispatcher {
    constexpr T* operator()(const Handle<T, Storage>& h) const = delete;
};

template <typename T, typename Storage> struct Handle {
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : _handle{ handle } {}
    constexpr Storage operator*() const { return _handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr auto* operator->() const { return HandleDispatcher<T>{}(*this); }

    Storage _handle{ ~Storage{ 0 } };
};

namespace std {
template <typename T> class hash<Handle<T>> {
  public:
    size_t operator()(const Handle<T>& h) const { return *h; }
};
} // namespace std