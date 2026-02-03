#pragma once

#include <cstdint>
#include <compare>
#include <concepts>
#include <atomic>
#include <cassert>

namespace eng
{
// Defines the underlying storage for handles, for each type separately.
// ENG_DEFINE_HANDLE_STORAGE can be used before first use of a handle
// with a particular type to specify different storage type, such as
// std::uintptr_t to allow storing regular pointers.
template <typename T> struct HandleStorage
{
    using type = uint32_t;
};

#define ENG_DEFINE_HANDLE_STORAGE(type, storage)                                                                       \
    template <> struct ::eng::HandleStorage<type>                                                                      \
    {                                                                                                                  \
        using type = storage;                                                                                          \
    }

struct GenerateHandle
{
};
inline constexpr GenerateHandle generate_handle{};

template <typename T> struct HandleGenerator
{
    static HandleStorage<T>::type gen() { return ++counter; }
    inline static std::atomic<typename HandleStorage<T>::storage_type> counter{ 0 };
};

template <typename T> struct HandleDispatcher
{
};

template <typename T> struct Handle
{
    using storage_type = typename HandleStorage<T>::type;
    constexpr Handle() = default;
    constexpr explicit Handle(storage_type handle) : handle{ handle } {}
    explicit Handle(GenerateHandle) : handle{ HandleGenerator<T>::gen() } {}
    constexpr storage_type operator*() const { return handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr explicit operator bool() const { return handle != ~storage_type{}; }

    auto* operator->() { return HandleDispatcher<T>{}(*this); }
    const auto* operator->() const { return HandleDispatcher<T>{}(*this); }

    auto& get() { return *HandleDispatcher<T>{}(*this); }
    auto& get() const { return *HandleDispatcher<T>{}(*this); }

    storage_type handle{ ~storage_type{} };
};

#define ENG_DEFINE_HANDLE_ALL_GETTERS(type, body)                                                                      \
    template <> struct HandleDispatcher<type>                                                                          \
    {                                                                                                                  \
        type* operator()(const Handle<type>& handle) const body                                                        \
    };
#define ENG_DEFINE_HANDLE_CONST_GETTERS(type, body)                                                                    \
    template <> struct HandleDispatcher<type>                                                                          \
    {                                                                                                                  \
        const type* operator()(const Handle<type>& handle) const body                                                  \
    };
} // namespace eng

namespace std
{
template <typename T> class hash<::eng::Handle<T>>
{
  public:
    size_t operator()(const ::eng::Handle<T>& h) const { return *h; }
};
} // namespace std
