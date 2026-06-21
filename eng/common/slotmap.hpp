#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include <deque>
#include <shared_mutex>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

namespace eng
{

using LockMutexTag = std::true_type;
using DontLockMutexTag = std::true_type;

/*
  Thread-safe object pool with a free list.
  First 32 bits of returned handle are an index which can be used to access associated data.
  Last 32 bits are tag.
 */
template <typename UserType, typename LockMutex = LockMutexTag> class MutexSlotmap
{

    struct Tag : public TypedId<Tag, u32>
    {
        using Base = TypedId<Tag, u32>;
        inline static constexpr u32 IDX_MASK = 0x7FFFFFFF;
        inline static constexpr u32 FREE_MASK = ~IDX_MASK;
        Tag() : Base() {}
        explicit Tag(u32 tag) : Base(tag) {}
        bool is_free() const { return (Base::handle & FREE_MASK) != 0; }
        void set_free(bool value)
        {
            if(value) { Base::handle |= FREE_MASK; }
            else { Base::handle = Base::handle & (~FREE_MASK); }
        }
        void inc() { Base::handle = (Base::handle & FREE_MASK) | (((Base::handle & IDX_MASK) + 1) & IDX_MASK); }
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

} // namespace eng