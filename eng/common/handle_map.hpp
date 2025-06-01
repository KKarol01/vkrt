#pragma once

#include <eng/common/handle.hpp>
#include <unordered_map>
#include <cstdint>
#include <concepts>

// template <typename T, typename Storage = uint32_t> using HandleMap = std::unordered_map<Handle<T, Storage>, T>;

template <typename T, typename Storage = uint32_t> class HandleMap
{
  public:
    using handle_t = Handle<T, Storage>;

    auto begin() { return storage.begin(); }
    auto end() { return storage.end(); }

    bool has(handle_t h) const { return storage.find(h) != storage.end(); }

    handle_t insert(const T& t)
    {
        const auto handle = handle_t{ generate_handle };
        if(handle) { storage.emplace(handle, t); }
        return handle;
    }

    handle_t insert(T&& t)
    {
        const auto handle = handle_t{ generate_handle };
        if(handle) { storage.emplace(handle, std::move(t)); }
        return handle;
    }

    template <typename... Args>
    handle_t emplace(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        return insert(T{ std::forward<Args>(args)... });
    }

    void erase(handle_t h)
    {
        auto it = storage.find(h);
        if(it != storage.end()) { storage.erase(it); }
    }

    T& at(handle_t h) { return storage.at(h); }
    const T& at(handle_t h) const { return storage.at(h); }

  private:
    std::unordered_map<handle_t, T> storage;
};
