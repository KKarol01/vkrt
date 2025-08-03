#pragma once

#include <cstdint>
#include <compare>
#include <atomic>

template <typename T, typename Storage = uint32_t> struct Handle;

template <typename T, typename Storage> struct HandleGenerator
{
    static Storage gen() { return ++counter; }
    inline static std::atomic<Storage> counter{ 0 };
};

struct HandleGenerate_T
{
};
inline constexpr HandleGenerate_T generate_handle{};

template <typename T, typename Storage> struct HandleDispatcher
{
};

// clang-format off
#define DEFINE_HANDLE_DISPATCHER(type, code)                                                                           \
    template <typename Storage> struct HandleDispatcher<type, Storage>                                                 \
    {                                                                                                                  \
        auto* get(auto handle) code                                                                                    \
    }
// clang-format on

template <typename T, typename Storage> struct Handle
{
    using Storage_T = Storage;
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : handle{ handle } {}
    explicit Handle(HandleGenerate_T) : handle{ HandleGenerator<T, Storage>::gen() } {}
    constexpr Storage operator*() const { return handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr explicit operator bool() const { return handle != ~Storage{}; }

    auto* operator->(this auto&& self)
    {
        auto dispatcher = HandleDispatcher<T, Storage>{};
        auto* tmp = dispatcher.get(self);
        return std::forward_like<decltype(self)>(tmp);
    }

    auto& get(this auto&& self)
    {
        auto dispatcher = HandleDispatcher<T, Storage>{};
        auto* tmp = dispatcher.get(self);
        return *std::forward_like<decltype(self)>(tmp);
    }

    Storage handle{ ~Storage{} };
};

namespace std
{
template <typename T> class hash<Handle<T>>
{
  public:
    size_t operator()(const Handle<T>& h) const { return *h; }
};
} // namespace std