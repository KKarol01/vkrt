#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <eng/common/logger.hpp>

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

  private:
    IndexType counter{};
    std::vector<IndexType> free_list;
};