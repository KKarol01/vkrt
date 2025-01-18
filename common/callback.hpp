#pragma once

#include <type_traits>
#include <utility>

template <typename R, typename... Args> struct Callback;

template <typename R, typename... Args> struct Callback<R(Args...)> {
    using CallbackPtr_T = R (*)(void*, Args...);
    constexpr Callback() = default;
    template <typename T> Callback(T&& t) : dispatch{ &Dispatch<std::decay_t<T>> }, target{ static_cast<void*>(&t) } {}
    template <typename T> static R Dispatch(void* target, Args... args) {
        return (*static_cast<T*>(target))(std::forward<Args>(args)...);
    }
    explicit operator bool() const { return !!target; }
    R operator()(Args... args) const { return dispatch(target, std::forward<Args>(args)...); }
    CallbackPtr_T dispatch{};
    void* target{};
};