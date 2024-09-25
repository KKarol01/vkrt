#pragma once

#include <cstdint>
#include <compare>
#include <atomic>

#define CREATE_HANDLE_DISPATCHER2(Type, ReturnType)                                                                    \
    template <typename Storage> struct HandleDispatcher<Type, Storage> {                                               \
        constexpr ReturnType* operator()(const Handle<Type, Storage>& h) const;                                        \
    };                                                                                                                 \
    template <typename Storage>                                                                                        \
    constexpr ReturnType* HandleDispatcher<Type, Storage>::operator()(const Handle<Type, Storage>& h) const

#define CREATE_HANDLE_DISPATCHER(Type) CREATE_HANDLE_DISPATCHER2(Type, Type)

template <typename T, typename Storage = uint32_t> struct Handle;

template <typename T, typename Storage = uint32_t> struct HandleDispatcher {
    constexpr T* operator()(const Handle<T, Storage>& h) const;
};

template <typename T, typename Storage> struct HandleGenerator {
    static Storage gen() { return counter.fetch_add(1); }
    inline static std::atomic<Storage> counter{ 0 };
};

struct HandleGenerate_T {};
inline constexpr HandleGenerate_T generate_handle{};

template <typename T, typename Storage> struct Handle {
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : _handle{ handle } {}
    constexpr explicit Handle(HandleGenerate_T) : _handle{ HandleGenerator<T, Storage>::gen() } {}
    constexpr Storage operator*() const { return _handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr auto* operator->() const { return HandleDispatcher<T>{}(*this); }
    constexpr explicit operator bool() const { return _handle != ~Storage{}; }
    Storage _handle{ ~Storage{} };
};

namespace std {
template <typename T> class hash<Handle<T>> {
  public:
    size_t operator()(const Handle<T>& h) const { return *h; }
};
} // namespace std