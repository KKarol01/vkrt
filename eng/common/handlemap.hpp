#pragma once

#include <concepts>
#include <cstdint>
#include <unordered_map>
#include "slotallocator.hpp"
#include "handle.hpp"

template <typename T, typename Hash = std::hash<T>, typename Storage = Handle<T>::Storage_T> class HandleMap
{
    using handle_t = Handle<T, Storage>;

  public:
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }

    auto size() const { return data.size(); }

    bool has(handle_t handle) const { return set.has(*handle); }

    T& at(handle_t h) { return data.at(h); }
    const T& at(handle_t h) const { return data.at(h); }

    template <typename Value>
    handle_t insert(Value&& v)
        requires std::constructible_from<T, Value>
    {
        return data.emplace(get_handle(), std::forward<Value>(v)).first->first;
    }

    template <typename... Args> handle_t emplace(Args&&... args) { return insert(T{ std::forward<Args>(args)... }); }

    void erase(handle_t handle)
    {
        if(!set.has(*handle)) { return; }
        set.free_slot(*handle);
        data.erase(handle);
    }

  private:
    handle_t get_handle() { return handle_t{ set.allocate_slot() }; }

    SlotAllocator set;
    std::unordered_map<handle_t, T> data;
};