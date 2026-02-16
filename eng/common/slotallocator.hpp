#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <eng/common/logger.hpp>
#include <eng/common/handle.hpp>

template <typename IndexType = uint32_t> class SlotAllocator
{
  public:
    IndexType allocate()
    {
        if(free_list.size() > 0)
        {
            const auto index = free_list.back();
            free_list.pop_back();
            return index;
        }
        if(counter == ~IndexType{})
        {
            ENG_ASSERT(false);
            return counter;
        }
        return counter++;
    }

    bool erase(IndexType index)
    {
        if(std::find(free_list.begin(), free_list.end(), index) != free_list.end()) { return false; }
        free_list.push_back(index);
        return true;
    }

    // Returns the number of active slots
    size_t size() const { return counter - (IndexType)free_list.size(); }

  private:
    IndexType counter{};
    std::vector<IndexType> free_list;
};

class VersionedSlotAllocator
{
  public:
    struct Slot : public eng::TypedIntegral<Slot, uint64_t>
    {
        using TypedIntegral<Slot, uint64_t>::TypedIntegral;
        Slot(uint32_t slot, uint32_t version) : TypedIntegral(((uint64_t)version << 32) | (uint64_t)slot) {}
        auto operator<=>(const Slot&) const = default;
        void inc_version() { *this = Slot{ slot(), version() + 1 }; }
        uint32_t slot() const { return (uint32_t)handle; }
        uint32_t version() const { return (uint32_t)(handle >> 32); }
    };

    Slot allocate()
    {
        if(free_list < slots.size()) { return slots[free_list++]; }
        slots.emplace_back(slots.size(), 0);
        ++free_list;
        return slots.back();
    }

    void erase(Slot slot)
    {
        if(!has(slot)) { return; }
        
    }

    bool has(Slot slot) const { return slot && slot.slot() < slots.size() && slot == slots[slot.slot()]; }

    std::vector<Slot> slots;
    size_t free_list{};
};
