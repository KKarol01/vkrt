#pragma once

#include "slotallocator.hpp"
#include "handle.hpp"
#include <concepts>

template <typename T, typename Hash = std::hash<T>> class SlotMap
{
  public:
    using index_t = SlotAllocator::index_t;

    auto begin() { return data.begin(); }
    auto end() { return data.end(); }

    bool has(index_t slot) const { return data.find(slot) != data.end(); }

    index_t insert(const T& t)
    {
        const auto idx = slots.allocate_slot();
        data[idx] = t;
        return idx;
    }

    index_t insert(T&& t)
    {
        const auto idx = slots.allocate_slot();
        data[idx] = std::move(t);
        return idx;
    }

    template <typename... Args>
    index_t emplace(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        const auto idx = slots.allocate_slot();
        data[idx] = T{ std::forward<Args>(t)... };
        return idx;
    }

    void erase(index_t slot)
    {
        if(slots.has(slot))
        {
            slots.free_slot(slot);
            data.at(slot).~T();
        }
    }

    bool has(index_t slot) const { return slots.has(slot); }

    T& at(index_t slot) { return data.at(slot); }
    const T& at(index_t slot) const { return data.at(slot); }

  private:
    SlotAllocator slots;
    std::unordered_map<size_t, T, Hash> data;
};

template <typename T, typename Hash = std::hash<T>, typename Storage = uint32_t> class HandleMap
{
  public:
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }

    Handle<T> insert(const T& t) { return Handle<T>{ data.insert(t) }; }
    Handle<T> insert(T&& t) { return Handle<T>{ data.insert(std::move(t)) }; }

    template <typename... Args>
    Handle<T> emplace(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        return Handle<T>{ data.emplace(std::forward<Args>(args)...) };
    }

    void erase(Handle<T> slot) { data.erase(*slot); }

    bool has(Handle<T> slot) const { return data.has(*slot); }

    T& at(Handle<T> h) { return data.at(*h); }
    const T& at(Handle<T> h) const { return data.at(*h); }

  private:
    SlotMap<T, Hash> data;
};
