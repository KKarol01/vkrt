#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

namespace eng
{

template <typename Storage = u32> struct SlotAllocator
{
    struct slot_t;
    using Slot = TypedId<slot_t, Storage>;

    bool has(Slot s) const { return *s < slots.size() && s == slots[*s]; }

    Slot::StorageType size() const { return num_slots; }

    Slot allocate()
    {
        if(num_slots == *Slot{}) { return Slot{}; }
        if(!next_free) { return slots.emplace_back(num_slots++); }
        Slot s = next_free;
        next_free = slots[*s]; // get next free
        slots[*s] = s;         // make slot valid
        ++num_slots;
        return s;
    }

    void erase(Slot s)
    {
        if(!has(s)) { return; }
        slots[*s] = next_free;
        next_free = s;
        --num_slots;
    }

    void erase(Storage s) { erase(Slot{ s }); }

    std::vector<Slot> slots;
    Slot next_free{};
    Storage num_slots{};
};

} // namespace eng
