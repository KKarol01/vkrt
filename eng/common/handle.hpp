#pragma once

#include <cstdint>
#include <compare>
#include <concepts>
#include <atomic>

namespace eng
{

template <typename T> struct Handle;

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

} // namespace eng

#define ENG_DEFINE_HANDLE_DISPATCHER(type)                                                                             \
    namespace eng                                                                                                      \
    {                                                                                                                  \
    template <> struct HandleDispatcher<type>                                                                          \
    {                                                                                                                  \
        inline static handle_dispatcher_get_pfunc_t<type> get_pfunc{};                                                 \
    };                                                                                                                 \
    }

#define ENG_SET_HANDLE_DISPATCHER(type, lambda_body)                                                                   \
    eng::HandleDispatcher<type>::get_pfunc = [](Handle<type> handle) -> type* lambda_body;

namespace eng
{

template <typename T>
concept handle_has_dispatcher = std::same_as<T*, decltype(HandleDispatcher<T>::get_pfunc(std::declval<Handle<T>>()))>;

template <typename T> struct HandleStorage
{
    using Storage_T = uint32_t;
};

#define ENG_DEFINE_HANDLE_STORAGE(type, storage)                                                                       \
    template <> struct ::eng::HandleStorage<type>                                                                      \
    {                                                                                                                  \
        using Storage_T = storage;                                                                                     \
    }

template <typename T> struct Handle
{
    using Storage_T = typename HandleStorage<T>::Storage_T;
    constexpr Handle() = default;
    constexpr explicit Handle(Storage_T handle) : handle{ handle } {}
    constexpr Handle(const Handle& handle) { *this = handle; }
    constexpr Handle& operator=(const Handle& handle)
    {
        this->handle = *handle;
        return *this;
    }
    explicit Handle(HandleGenerate_T) : handle{ HandleGenerator<T, Storage_T>::gen() } {}
    constexpr Storage_T operator*() const { return handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr explicit operator bool() const { return handle != ~Storage_T{}; }

    auto& get(this auto&& self)
        requires handle_has_dispatcher<T>
    {
        return *HandleDispatcher<T>::get_pfunc(self);
    }
    auto* operator->(this auto&& self)
        requires handle_has_dispatcher<T>
    {
        return HandleDispatcher<T>::get_pfunc(self);
    }

    Storage_T handle{ ~Storage_T{} };
};

} // namespace eng

namespace std
{
template <typename T> class hash<eng::Handle<T>>
{
  public:
    size_t operator()(const eng::Handle<T>& h) const { return *h; }
};
} // namespace std