#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <deque>
#include <type_traits>
#include <bit>
#include <variant>
#include <compare>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

namespace eng
{

struct SlotHandle
{
    constexpr SlotHandle() = default;
    constexpr SlotHandle(u32 value) : index(value & 0xFFFFF), version(((value) >> 24) & 0xFF) {}
    constexpr SlotHandle(u32 index, u8 version) : index(index), version(version) {}
    constexpr u32 operator*() const { return std::bit_cast<u32>(*this); }
    explicit constexpr operator bool() const { return *this != SlotHandle{}; }
    constexpr auto operator<=>(const SlotHandle&) const = default;
    constexpr SlotHandle inc() const { return SlotHandle{ (u32)index, (u8)(version + 1) }; }
    u32 index : 24 { 0xFFFFF };
    u32 version : 8 { 0xFF };
};

template <typename UserType> class Slotmap
{
    inline static UserType user_null_obj{};

    struct Slot
    {
        std::variant<SlotHandle, UserType> storage;
        SlotHandle tag{};
    };

  public:
    auto& operator[](this auto& self, SlotHandle tag) { return self.at(tag); }

    auto& at(this auto& self, SlotHandle tag)
    {
        if(!tag || self.m_slots[tag.index].tag != tag)
        {
            ENG_ASSERT(false, "Invalid id");
            return user_null_obj;
        }
        return std::get<UserType>(self.m_slots[tag.index].storage);
    }

    template <typename... Args> SlotHandle emplace(Args&&... args)
    {
        if(next_free)
        {
            SlotHandle handle = next_free;
            next_free = std::get<SlotHandle>(m_slots[handle.index].storage);
            m_slots[handle.index].tag = handle;
            m_slots[handle.index].storage.template emplace<UserType>(std::forward<Args>(args)...);
            return handle;
        }

        SlotHandle handle{ (u32)m_slots.size(), 0 };
        m_slots.emplace_back();
        m_slots.back().tag = handle;
        m_slots.back().storage.template emplace<UserType>(std::forward<Args>(args)...);
        return handle;
    }

    void erase(SlotHandle tag)
    {
        if(!tag || m_slots[tag.index].tag != tag)
        {
            ENG_ASSERT(false, "Invalid id");
            return;
        }

        m_slots[tag.index].tag = m_slots[tag.index].tag.inc();
        m_slots[tag.index].storage = next_free;
        next_free = m_slots[tag.index].tag;
    }

  private:
    std::vector<Slot> m_slots;
    SlotHandle next_free;
};

} // namespace eng