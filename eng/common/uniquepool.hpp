#pragma once

#include <concepts>
#include <mutex>
#include <unordered_map>
#include <eng/common/pool.hpp>

namespace eng
{

template <typename T>
concept UniquePoolCompatible = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
    { std::hash<T>{}(a) } -> std::convertible_to<u64>;
};

/*
  Pool that stores each distinct value once. Inserting a value equal to an already stored one
  returns the handle of the stored one. Elements are const, because changing them would change their hash.
  The lookup map is keyed by pointers into the pool, which is possible thanks to the pool's stable addressing.
 */
template <UniquePoolCompatible T, typename Hash = std::hash<T>, typename EqualTo = std::equal_to<T>> class UniquePool
{
  public:
    using handle_t = Handle<T>;

    struct InsertionResult
    {
        handle_t handle;
        bool success; // false if the value was already in the pool or the pool is full
    };

  private:
    // Keys are pointers into the pool, so hash and compare the pointed-to values.
    struct KeyHash
    {
        usize operator()(const T* t) const { return Hash{}(*t); }
    };
    struct KeyEqual
    {
        bool operator()(const T* a, const T* b) const { return EqualTo{}(*a, *b); }
    };

  public:
    template <typename Arg>
        requires std::same_as<std::remove_cvref_t<Arg>, std::remove_cvref_t<T>>
    InsertionResult insert(Arg&& t)
    {
        std::scoped_lock lock{ m_mutex };
        if(auto it = m_lookup.find(&t); it != m_lookup.end()) { return { it->second, false }; }
        const auto h = m_pool.emplace(std::forward<Arg>(t));
        if(!h) { return { handle_t{}, false }; }
        m_lookup.emplace(m_pool.try_get(h), h);
        return { h, true };
    }

    handle_t find(const T& t) const
    {
        std::scoped_lock lock{ m_mutex };
        auto it = m_lookup.find(&t);
        return it != m_lookup.end() ? it->second : handle_t{};
    }

    bool erase(handle_t h)
    {
        std::scoped_lock lock{ m_mutex };
        const auto* ptr = m_pool.try_get(h);
        if(!ptr) { return false; }
        m_lookup.erase(ptr);
        return m_pool.erase(h);
    }

    // Returns nullptr when the handle is null or stale.
    const T* try_get(handle_t h) const { return m_pool.try_get(h); }

  private:
    Pool<T> m_pool;
    std::unordered_map<const T*, handle_t, KeyHash, KeyEqual> m_lookup;
    mutable std::mutex m_mutex;
};

} // namespace eng
