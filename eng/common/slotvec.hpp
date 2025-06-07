#pragma once

#include <concepts>
#include <vector>
#include <bit>

template <std::integral TIndex> class SlotVec
{
    static inline constexpr auto TBITS = sizeof(TIndex) * 8;

  public:
    TIndex allocate_slot()
    {
        TIndex idx = 0;
        for(auto& s : slots)
        {
            const auto slot_idx = std::countl_one(s);
            if(slot_idx == TBITS) { idx += TBITS; }
            else
            {
                s |= (1ull << (TBITS - 1 - slot_idx));
                return idx + slot_idx;
            }
        }
        slots.push_back(1ull << (TBITS - 1));
        return idx;
    }

    void free_slot(TIndex slot)
    {
        const auto slot_idx = slot / TBITS;
        const auto bit_idx = slot % TBITS;
        if(slots.size() <= slot_idx) { return; }
        slots.at(slot_idx) &= ~(1ull << (TBITS - 1 - bit_idx));
    }

  private:
    std::vector<TIndex> slots;
};