#pragma once

#include <vector>
#include <bit>

class SlotAllocator
{
  public:
    using index_t = size_t;
    static inline constexpr size_t BITS = sizeof(index_t) * 8;

    index_t allocate_slot()
    {
        for(index_t i = 0; i < pages.size(); ++i)
        {
            auto& page = pages.at(i);
            const auto bit = std::countl_one(page);
            if(bit == BITS) { continue; }
            page |= make_bit_mask(bit);
            return i * BITS + bit;
        }
        pages.push_back(make_bit_mask(0));
        return (pages.size() - 1) * BITS;
    }

    void free_slot(index_t slot)
    {
        const auto page = slot / BITS;
        const auto bit = slot % BITS;
        if(pages.size() <= page) { return; }
        pages.at(page) &= ~make_bit_mask(bit);
    }

    bool has(index_t slot) const
    {
        const auto page = slot / BITS;
        const auto bit = slot % BITS;
        return page < pages.size() && (pages.at(page) & make_bit_mask(bit));
    }

  private:
    static index_t make_bit_mask(index_t bit_idx) { return index_t{ 1 } << (BITS - 1 - bit_idx); }
    std::vector<index_t> pages;
};