#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

namespace eng
{
/*
 * Uses versioned index that has a version (generation) and a index.
 * When erasing, returned index gets it's version incremented by 1 (with wraparound),
 * and gets added to the free list chain.
 * Any slot is valid if it's index corresponds to the slot's place in the array.
 */
template <typename Storage = uint32_t, Storage INDEX_BITS = 21> struct SlotAllocator
{
    using Slot = VersionedIndex<Storage, INDEX_BITS>;

    bool has(Slot s) const { return s && s.get_index() < slots.size() && s == slots[s.get_index()]; }

    Slot::StorageType size() const { return num_slots; }

    Slot allocate()
    {
        if(!next_free) { return slots.emplace_back(Slot{ (uint32_t)slots.size(), 0u }); }
        const auto ret = next_free;
        std::swap(next_free, slots[next_free.get_index()]);
        ++num_slots;
        return ret;
    }

    void erase(Slot s)
    {
        if(!has(s)) { return; }
        const auto si = s.get_index();
        const auto nv = s.get_version() + 1;
        slots[si] = Slot{ si, nv };

        Slot* cf = &next_free;
        Slot* pf{};
        while(*cf && cf->get_version() < nv)
        {
            cf = &slots[cf->get_index()];
            pf = cf;
        }
        if(!pf) { pf = cf; }
        std::swap(slots[si], *pf);
        --num_slots;
    }

    std::vector<Slot> slots;
    Slot next_free{};
    Slot::StorageType num_slots{};
};

} // namespace eng
