#pragma once

#include <cstdint>
#include <compare>
#include <concepts>
#include <atomic>
#include <cassert>
#include <type_traits>

namespace eng
{
// Defines the underlying storage for handles, for each type separately.
// ENG_DEFINE_HANDLE_STORAGE can be used before first use of a handle
// with a particular type to specify different storage type, such as
// std::uintptr_t to allow storing regular pointers.
template <typename T> struct HandleStorage
{
    using storage_type = uint32_t;
};

#define ENG_DEFINE_HANDLE_STORAGE(type, storage)                                                                       \
    template <> struct ::eng::HandleStorage<type>                                                                      \
    {                                                                                                                  \
        using storage_type = storage;                                                                                  \
    }

struct GenerateHandle
{
};
inline constexpr GenerateHandle generate_handle{};

template <typename T> struct HandleGenerator
{
    static HandleStorage<T>::storage_type gen() { return ++counter; }
    inline static std::atomic<typename HandleStorage<T>::storage_type> counter{ 0 };
};

template <typename T> struct HandleDispatcher
{
};

template <std::integral StorageType> struct TypedIntegral
{
    using storage_type = StorageType;
    constexpr TypedIntegral() = default;
    constexpr explicit TypedIntegral(storage_type handle) : handle{ handle } {}
    constexpr storage_type operator*() const { return handle; }
    constexpr auto operator<=>(const TypedIntegral& h) const = default;
    constexpr explicit operator bool() const { return handle != ~storage_type{}; }
    storage_type handle{ ~storage_type{} };
};

template <typename T, std::integral StorageType = typename HandleStorage<T>::storage_type>
struct Handle : TypedIntegral<StorageType>
{
    using TypedIntegral<StorageType>::TypedIntegral;
    auto* operator->() { return HandleDispatcher<T>{}(*this); }
    const auto* operator->() const { return HandleDispatcher<T>{}(*this); }
    auto& get() { return *HandleDispatcher<T>{}(*this); }
    auto& get() const { return *HandleDispatcher<T>{}(*this); }
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
