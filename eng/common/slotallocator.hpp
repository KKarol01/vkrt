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
        if(next_free == ~0u) { return ~0u; }
        ++num_slots;
        if(slots.size() == next_free)
        {
            slots.emplace_back(next_free);
            used.emplace_back(true);
            return next_free++;
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
        --num_slots;
        used[slot] = false;
        slots[slot] = next_free;
        next_free = slot;
    }

    bool has(uint32_t slot) const { return slot < slots.size() && used[slot]; }

    uint32_t size() const { return num_slots; }

  private:
    std::vector<uint32_t> slots;
    std::vector<bool> used;
    uint32_t next_free{};
    uint32_t num_slots{};
};
