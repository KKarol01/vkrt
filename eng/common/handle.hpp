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

// Use with ENG_DEFINE_HANDLE_ALL_GETTERS and ENG_DEFINE_HANDLE_CONST_GETTERS
// to create specializations with non-static member functions named get()
// that will be used with get() and operator-> member functions inside Handle structure itself.
// Main idea: allow handles to be used as if they were pointers (however operator*() returns
// the underlying value, not dereferences it).
// if ENG_DEFINE_HANDLE_ALL_GETTERS was invoked before first use (instantiation),
// if handle is access as const object, return const reference to represented type,
// if not, return non-const reference.
// if ENG_DEFINE_HANDLE_CONST_GETTERS was invoked, always return const.
// All const variant is useful when representing types stored in sets whose
// modification would invalidate the set or it would have to be rehashed.
// if neither were called, rely on the language not instantiating unused
// member functions of templated type (handlegetter::get does not exist and
// it wouldn't compile otherwise).
template <typename T> struct HandleGetter
{
};

// instructs non-const get() inside Handle struct to be created and not
// fail the SFINAE test. the member function itself has to be templated
// because HandleHasNonConstGetter::value is not dependent on the T type
// and in that case, the enable_if_t wouldn't be allowed to disable this get().
// also, get() wouldn't be a template, so couldn't be disabled.
template <typename T> struct HandleHasNonConstGetter : std::false_type
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

    template <bool enable = HandleHasNonConstGetter<T>::value> std::enable_if_t<enable, T&> get()
    {
        return *HandleGetter<T>{}.get(*this);
    }
    const T& get() const
    {
        const HandleGetter<T> g{};
        return *g.get(*this);
    }

    template <bool enable = HandleHasNonConstGetter<T>::value> std::enable_if_t<enable, T*> operator->()
    {
        return &get();
    }
    const T* operator->() const { return &get(); }

    storage_type handle{ ~storage_type{} };
};

#define ENG_DEFINE_HANDLE_ALL_GETTERS(type)                                                                            \
    namespace eng                                                                                                      \
    {                                                                                                                  \
    template <> struct HandleHasNonConstGetter<type> : std::true_type                                                  \
    {                                                                                                                  \
    };                                                                                                                 \
    template <> struct HandleGetter<type>                                                                              \
    {                                                                                                                  \
        static inline type* (*pfunc)(Handle<type> handle);                                                             \
        type* get(Handle<type> handle)                                                                                 \
        {                                                                                                              \
            assert(pfunc && "pfunc must be set prior to the first use.");                                              \
            return pfunc(handle);                                                                                      \
        }                                                                                                              \
        const type* get(Handle<type> handle) const                                                                     \
        {                                                                                                              \
            assert(pfunc && "pfunc must be set prior to the first use.");                                              \
            return pfunc(handle);                                                                                      \
        }                                                                                                              \
    };                                                                                                                 \
    }
#define ENG_DEFINE_HANDLE_CONST_GETTERS(type)                                                                          \
    namespace eng                                                                                                      \
    {                                                                                                                  \
    template <> struct HandleGetter<type>                                                                              \
    {                                                                                                                  \
        static inline const type* (*pfunc)(Handle<type> handle);                                                       \
        const type* get(Handle<type> handle) const                                                                     \
        {                                                                                                              \
            assert(pfunc && "pfunc must be set prior to the first use.");                                              \
            return pfunc(handle);                                                                                      \
        }                                                                                                              \
    };                                                                                                                 \
    }
#define ENG_SET_HANDLE_GETTERS(type, lambda_body)                                                                      \
    ::eng::HandleGetter<type>::pfunc = [](::eng::Handle<type> handle) lambda_body
} // namespace eng

namespace std
{
template <typename T> class hash<::eng::Handle<T>>
{
  public:
    size_t operator()(const ::eng::Handle<T>& h) const { return *h; }
};
} // namespace std
