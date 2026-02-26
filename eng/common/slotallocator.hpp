#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <eng/common/logger.hpp>
#include <eng/common/types.hpp>

namespace eng
{

struct slots_versioned_tag;
struct slots_not_versioned_tag;

/*
 * Uses versioned index that has a version (generation) and a index.
 * When erasing, returned index gets it's version incremented by 1 (with wraparound),
 * and gets added to the free list chain.
 * Any slot is valid if it's index corresponds to the slot's place in the array.
 */
template <typename Storage = uint32_t, typename Versioning = slots_versioned_tag> struct SlotAllocator
{
    struct slot_t;
    using VersionedSlotType = VersionedIndex<slot_t, Storage>;
    using NonVersionedSlotType = TypedId<slot_t, Storage>;
    using Slot = std::conditional_t<std::is_same_v<Versioning, slots_versioned_tag>, VersionedSlotType, NonVersionedSlotType>;

    bool has(Slot s) const { return s && get_index(s) < slots.size() && s == slots[get_index(s)]; }

    Slot::StorageType size() const { return num_slots; }

    Slot allocate()
    {
        if(!next_free) { return slots.emplace_back(make_slot((uint32_t)slots.size(), 0u)); }
        const auto ret = next_free;
        std::swap(next_free, slots[get_index(next_free)]);
        ++num_slots;
        return ret;
    }

    void erase(Slot s)
    {
        if(!has(s)) { return; }
        const auto si = get_index(s);
        const auto nv = get_version(s) + 1;
        slots[si] = make_slot(si, nv);

        Slot* cf = &next_free;
        Slot* pf{};
        while(*cf && get_version(*cf) < nv)
        {
            cf = &slots[get_index(*cf)];
            pf = cf;
        }
        if(!pf) { pf = cf; }
        std::swap(slots[si], *pf);
        --num_slots;
    }

    static Slot::StorageType get_index(Slot s)
    {
        if constexpr(std::is_same_v<Versioning, slots_versioned_tag>) { return s.get_index(); }
        else { return *s; }
    }

    static Slot::StorageType get_version(Slot s)
    {
        if constexpr(std::is_same_v<Versioning, slots_versioned_tag>) { return s.get_version(); }
        else { return {}; }
    }

    static Slot make_slot(Slot::StorageType index, Slot::StorageType version)
    {
        if constexpr(std::is_same_v<Versioning, slots_versioned_tag>) { return Slot{ index, version }; }
        else { return Slot{ index }; }
    }

    std::vector<Slot> slots;
    Slot next_free{};
    Slot::StorageType num_slots{};
};

} // namespace eng
