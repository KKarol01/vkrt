#pragma once

#include <concepts>
#include <cstdint>
#include <unordered_set>
#include "handle.hpp"

template <typename T, typename RT = std::remove_cvref_t<T>>
concept HandleSetCompatible = requires(const RT& v) {
    { std::hash<RT>{}(v) } -> std::convertible_to<size_t>;
    { std::equal_to<T>{}(std::declval<T>()) } -> std::convertible_to<bool>;
};

template <typename T, typename Hash = std::hash<T>, typename Storage = Handle<T>::Storage_T>
    requires HandleSetCompatible<T>
class HandleSet
{
    using handle_t = Handle<T, Storage>;

  public:
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }

    auto size() const { return data.size(); }

    bool has(handle_t h) const { return data.contains(*handle_to_ptr(t)); }
    bool has(const T& t) const { return data.contains(t); }

    const T& at(handle_t h) const { return *handle_to_ptr(h); }

    template <typename Value>
    handle_t insert(Value&& v)
        requires std::same_as<T, std::remove_cvref_t<Value>>
    {
        return ptr_to_handle(&*data.insert(std::forward<Value>(v)));
    }

    template <typename... Args>
    handle_t emplace(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        return insert(T{ std::forward<Args>(args)... });
    }

    void erase(handle_t handle) { data.erase(*handle_to_ptr(handle)); }

  private:
    static constexpr T* handle_to_ptr(handle_t h) { return reinterpret_cast<T*>(*handle); }
    static constexpr handle_t ptr_to_handle(const T* t) { return handle_t{ reinterpret_cast<uintptr_t>(t) }; }
    std::unordered_multiset<T, Hash> data;
};