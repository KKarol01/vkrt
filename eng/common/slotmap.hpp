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

} // namespace eng