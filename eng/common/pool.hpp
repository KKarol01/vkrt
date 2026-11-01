#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <eng/common/handle.hpp>
#include <eng/common/logger.hpp>

namespace eng
{

/*
  Paged object pool with stable addresses and generation-checked handles.
  Pages are allocated on demand and never move, so pointers and references
  to elements stay valid until the element is erased.
  Lookups (try_get) are lock-free; emplace and erase take a mutex.
 */
template <typename T, u32 PAGE_SIZE = 256, u32 NUM_PAGES = 4096> class Pool
{
  public:
    using handle_t = Handle<T>;
    inline static constexpr u64 MAX_ELEMENTS = (u64)PAGE_SIZE * NUM_PAGES;
    static_assert(MAX_ELEMENTS < (1ull << handle_t::INDEX_BITS),
                  "Pool can't be fully addressed by handle's index bits.");

  private:
    struct Slot
    {
        inline static constexpr u32 NULL_FREE = (1u << 31) - 1u;
        static_assert(MAX_ELEMENTS <= NULL_FREE, "Pool can't be fully addressed by Slot::next_free.");
        alignas(T) std::byte storage[sizeof(T)];
        u32 gen{};
        u32 next_free : 31 { NULL_FREE };
        u32 alive : 1 { 0 };
    };

  public:
    ~Pool()
    {
        const auto count = m_next.load();
        for(auto i = 0u; i < count; ++i)
        {
            auto& s = slot(i);
            if(s.alive) { std::destroy_at(data(s)); }
        }
        for(auto& p : m_pages)
        {
            delete[] p.load();
        }
    }

    template <typename... Args> handle_t emplace(Args&&... args)
    {
        std::scoped_lock lock{ m_mutex };
        u32 index{};
        if(m_free_head != Slot::NULL_FREE)
        {
            index = m_free_head;
            m_free_head = slot(index).next_free;
        }
        else
        {
            index = m_next.load();
            if(index == MAX_ELEMENTS)
            {
                ENG_ASSERT(false, "Pool is full.");
                return handle_t{};
            }
            auto& page = m_pages[index / PAGE_SIZE];
            if(!page.load()) { page.store(new Slot[PAGE_SIZE]{}); }
            m_next.store(index + 1);
        }
        auto& s = slot(index);
        std::construct_at(data(s), std::forward<Args>(args)...);
        s.next_free = Slot::NULL_FREE;
        s.alive = 1;
        return handle_t{ index, s.gen };
    }

    // Returns false on null handle. Asserts and returns false on stale handle.
    bool erase(handle_t h)
    {
        if(!h) { return false; }
        std::scoped_lock lock{ m_mutex };
        auto* s = find_slot(h);
        if(!s)
        {
            ENG_ASSERT(false, "Erasing stale handle.");
            return false;
        }
        std::destroy_at(data(*s));
        s->alive = 0;
        s->gen = (s->gen + 1) & handle_t::GEN_MASK;
        s->next_free = m_free_head;
        m_free_head = h.index();
        return true;
    }

    // Returns nullptr when the handle is null or stale.
    T* try_get(handle_t h)
    {
        auto* s = find_slot(h);
        return s ? data(*s) : nullptr;
    }
    const T* try_get(handle_t h) const { return const_cast<Pool*>(this)->try_get(h); }

    // Returns handle to the first alive element satisfying pred(const T&), or null handle.
    handle_t find_if(auto&& pred)
    {
        const auto count = m_next.load();
        for(auto i = 0u; i < count; ++i)
        {
            auto& s = slot(i);
            if(s.alive && pred(*data(s))) { return handle_t{ i, s.gen }; }
        }
        return handle_t{};
    }

  private:
    static T* data(Slot& s) { return reinterpret_cast<T*>(s.storage); }

    // Only for indices below m_next, which always have their page allocated.
    Slot& slot(u32 index) { return m_pages[index / PAGE_SIZE].load()[index % PAGE_SIZE]; }

    Slot* find_slot(handle_t h)
    {
        if(!h || h.index() >= MAX_ELEMENTS) { return nullptr; }
        auto* page = m_pages[h.index() / PAGE_SIZE].load();
        if(!page) { return nullptr; }
        auto& s = page[h.index() % PAGE_SIZE];
        if(!s.alive || s.gen != h.generation()) { return nullptr; }
        return &s;
    }

    std::array<std::atomic<Slot*>, NUM_PAGES> m_pages{};
    std::mutex m_mutex;
    u32 m_free_head{ Slot::NULL_FREE };
    std::atomic<u32> m_next{};
};

} // namespace eng
