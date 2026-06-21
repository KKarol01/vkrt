#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include <deque>
#include <shared_mutex>
#include <type_traits>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

namespace eng
{

using LockMutexTag = std::true_type;
using DontLockMutexTag = std::false_type;

/*
  Thread-safe object pool with a free list.
  First 32 bits of returned handle are an index which can be used to access associated data.
  Last 32 bits are tag.
 */
template <typename UserType, typename LockMutex = LockMutexTag> class MutexSlotmap
{
    struct DummySharedMutex
    {
        void lock() {}
        void unlock() {}
        void lock_shared() {}
        void unlock_shared() {}
    };
    using SharedMutex = std::conditional_t<LockMutex::value, std::shared_mutex, DummySharedMutex>;
    struct Tag : public TypedId<Tag, u32>
    {
        using Base = TypedId<Tag, u32>;
        using Base::Base;
        Tag(u32 index, u32 version) : Base(((version & 0xFF) << 24u) | (index & 0xFFFFFFu)) {}
        u32 get_index() const { return Base::handle & 0xFFFFFFu; }
        u32 get_version() const { return (Base::handle >> 24u) & 0xFF; }
        Tag inc_version() const { return Tag{ get_index(), get_version() + 1 }; }
    };
    struct Slot
    {
        UserType data;
        Tag tag{};
    };

  public:
    auto& operator[](this auto& self, u32 id) { return self.at(id); }

    auto& at(this auto& self, u32 id)
    {
        std::shared_lock lock{ self.m_mutex };
        Tag tag{ id };
        if(!tag || self.m_slots[tag.get_index()].tag != tag)
        {
            static UserType null_type{};
            ENG_ASSERT(false, "Invalid id");
            return null_type;
        }
        return self.m_slots[tag.get_index()].data;
    }

    template <typename... Args> u32 emplace(Args&&... args)
    {
        std::scoped_lock lock{ m_mutex };
        Tag tag;
        if(!m_free.empty())
        {
            tag = m_free.back();
            m_free.pop_back();
            std::construct_at(&m_slots[tag.get_index()].data, std::forward<Args>(args)...);
        }
        else
        {
            tag = Tag{ (u32)m_slots.size(), 0 };
            m_slots.emplace_back(UserType{ std::forward<Args>(args)... }, tag);
        }
        return *tag;
    }

    void erase(u32 id)
    {
        std::scoped_lock lock{ m_mutex };
        Tag tag{ id };
        if(!tag || m_slots[tag.get_index()].tag != tag)
        {
            ENG_ASSERT(false, "Invalid id");
            return;
        }
        tag = tag.inc_version();
        m_slots[tag.get_index()].tag = tag;
        std::destroy_at(&m_slots[tag.get_index()].data);
        m_free.push_back(tag);
    }

  private:
    std::vector<Slot> m_slots;
    std::deque<Tag> m_free;
    SharedMutex m_mutex;
};

template <typename T> using Slotmap = MutexSlotmap<T, DontLockMutexTag>;

#if 0
template <typename UserType, typename LockMutex = LockMutexTag> class MutexSlotmap
{
    struct Tag : public TypedId<Tag, u32>
    {
        using Base = TypedId<Tag, u32>;
        inline static constexpr u32 IDX_MASK = 0x7FFFFFFF;
        inline static constexpr u32 FREE_MASK = ~IDX_MASK;
        Tag() : Base() {}
        explicit Tag(u32 tag) : TypedId(tag) {}
        bool is_free() const { return (handle & FREE_MASK) != 0; }
        void set_free(bool value)
        {
            if(value) { handle |= FREE_MASK; }
            else { handle = handle & (~FREE_MASK); }
        }
        void inc() { handle = (handle & FREE_MASK) | (((handle & IDX_MASK) + 1) & IDX_MASK); }
    };

    struct Slot
    {
        UserType data;
        Tag tag;
    };

  public:
    struct PublicId : public TypedId<PublicId, u64>
    {
        using Base = TypedId<PublicId, u64>;
        PublicId() : Base() {}
        explicit PublicId(u64 id) : Base(id) {}
        PublicId(u32 idx, u32 tag) : PublicId(((u64)tag << 32) | (u64)idx) {}
        u32 get_idx() const { return (u32)Base::handle; }
        Tag get_tag() const { return Tag{ (u32)(Base::handle >> 32u) }; }
    };

    auto& at(this auto& self, u64 id)
    {
        PublicId pubid{ id };
        if(!pubid) { return null_object; }
        std::shared_lock lock = self.get_shared_lock();
        auto& slot = self.m_slots_vec[pubid.get_idx()];
        if(slot.tag != pubid.get_tag())
        {
            ENG_ASSERT(false, "Invalid id");
            return null_object;
        }
        return slot.data;
    }

    auto& operator[](this auto& self, u64 id) { return self.at(id); }

    template <typename... Args> PublicId emplace(Args&&... args)
    {
        std::unique_lock lock = get_exclusive_lock();
        if(!m_free_vec.empty())
        {
            const auto idx = m_free_vec.back();
            m_free_vec.pop_back();

            Slot& slot = m_slots_vec[idx];
            slot.tag.set_free(false);
            // index reserved, unlock
            unlock_exclusive_lock(lock);

            std::construct_at(&slot.data, std::forward<Args>(args)...);
            return PublicId{ idx, *slot.tag };
        }

        PublicId pubid{ (u32)m_slots_vec.size(), 0u };
        m_slots_vec.emplace_back(UserType{ std::forward<Args>(args)... }, pubid.get_tag());

        return pubid;
    }

    void erase(u64 id)
    {
        PublicId pubid{ id };
        if(!pubid) { return; }

        std::unique_lock lock = get_exclusive_lock();
        if(pubid.get_idx() >= m_slots_vec.size()) { return; }
        Slot& slot = m_slots_vec[pubid.get_idx()];
        if(slot.tag != pubid.get_tag() || slot.tag.is_free()) { return; }

        slot.tag.set_free(true);
        slot.tag.inc();
        unlock_exclusive_lock(lock);
        std::destroy_at(&slot.data);
        lock_exclusive_lock(lock);
        m_free_vec.push_back(pubid.get_idx());
    }

  private:
    std::shared_lock<std::shared_mutex> get_shared_lock()
    {
        if constexpr(LockMutex()) { return std::shared_lock{ m_mutex }; }
        return std::shared_lock<std::shared_mutex>{};
    }
    std::unique_lock<std::shared_mutex> get_exclusive_lock()
    {
        if constexpr(LockMutex()) { return std::unique_lock{ m_mutex }; }
        return std::unique_lock<std::shared_mutex>{};
    }
    void lock_exclusive_lock(auto& lock)
    {
        if constexpr(LockMutex()) { lock.lock(); }
    }
    void unlock_exclusive_lock(auto& lock)
    {
        if constexpr(LockMutex()) { lock.unlock(); }
    }
    void lock_shared()
    {
        if constexpr(LockMutex()) { m_mutex.lock_shared(); }
    }
    void unlock_shared()
    {
        if constexpr(LockMutex()) { m_mutex.unlock_shared(); }
    }
    void lock_exclusive()
    {
        if constexpr(LockMutex()) { m_mutex.lock(); }
    }
    void unlock_exclusive()
    {
        if constexpr(LockMutex()) { m_mutex.unlock(); }
    }

    inline static UserType null_object{};
    std::deque<Slot> m_slots_vec;
    std::deque<u32> m_free_vec;
    std::shared_mutex m_mutex;
};

template <typename T> using Slotmap = MutexSlotmap<T, DontLockMutexTag>;
#endif
} // namespace eng