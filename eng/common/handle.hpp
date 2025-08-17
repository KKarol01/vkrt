#pragma once

#include <cstdint>
#include <compare>
#include <concepts>
#include <atomic>

template <typename T, typename Storage = uint32_t> struct Handle;

template <typename T> using handle_dispatcher_get_pfunc_t = T* (*)(Handle<T>);

struct HandleGenerate_T
{
};
inline constexpr HandleGenerate_T generate_handle{};

template <typename T, typename Storage> struct HandleGenerator
{
    static Storage gen() { return ++counter; }
    inline static std::atomic<Storage> counter{ 0 };
};

template <typename T> struct HandleDispatcher
{
};

#define ENG_DEFINE_HANDLE_DISPATCHER(type)                                                                             \
    template <> struct HandleDispatcher<type>                                                                          \
    {                                                                                                                  \
        inline static handle_dispatcher_get_pfunc_t<type> get_pfunc{};                                                 \
    }

#define ENG_SET_HANDLE_DISPATCHER(type, lambda_body)                                                                   \
    HandleDispatcher<type>::get_pfunc = [](Handle<type> handle) -> type* lambda_body

template <typename T>
concept handle_has_dispatcher = std::same_as<T*, decltype(HandleDispatcher<T>::get_pfunc(std::declval<Handle<T>>()))>;

template <typename T, typename Storage> struct Handle
{
    using Storage_T = Storage;
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : handle{ handle } {}
    constexpr Handle(const Handle& handle) { *this = handle; }
    constexpr Handle& operator=(const Handle& handle)
    {
        this->handle = *handle;
        return *this;
    }
    explicit Handle(HandleGenerate_T) : handle{ HandleGenerator<T, Storage>::gen() } {}
    constexpr Storage operator*() const { return handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr explicit operator bool() const { return handle != ~Storage{}; }

    auto& get(this auto&& self)
        requires handle_has_dispatcher<T>
    {
        return *HandleDispatcher<T>::get_pfunc(self);
        // return std::forward_like<std::add_lvalue_reference_t<decltype(self)>>(*HandleDispatcher<T>::get_pfunc(self));
    }
    auto* operator->(this auto&& self)
        requires handle_has_dispatcher<T>
    {
        return HandleDispatcher<T>::get_pfunc(self);
        // return std::forward_like<std::remove_reference_t<decltype(self)>>(HandleDispatcher<T>::get_pfunc(self));
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