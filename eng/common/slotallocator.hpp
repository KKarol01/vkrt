#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <eng/common/logger.hpp>
#include <eng/common/handle.hpp>

struct SlotAllocator
{
    uint32_t allocate()
    {
        if(next_free == ~0u)
        {
            used.push_back(true);
            return slots.emplace_back(slots.size());
        }
        if(next_free >= slots.size() || used[next_free])
        {
            ENG_ASSERT(false);
            return ~0u;
        }
        const auto slot = next_free;
        next_free = slots[slot];
        slots[slot] = slot;
        used[slot] = true;
        return slot;
    }

    void erase(uint32_t slot)
    {
        if(!used[slot]) { return; }
        used[slot] = false;
        slots[slot] = next_free;
        next_free = slot;
    }

    bool has(uint32_t slot) const { return slot != ~0u && slot < used.size() && used[slot]; }

    std::vector<uint32_t> slots;
    std::vector<bool> used;
    uint32_t next_free{ ~0u };
};
