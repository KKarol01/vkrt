#pragma once

#include <cstdint>
#include <vector>
#include <eng/common/slotallocator.hpp>
#include <eng/common/logger.hpp>

template <typename UserType, typename IndexType = uint32_t> class Slotmap
{
  public:
    struct Index
    {
        inline static constexpr IndexType NULL_INDEX = IndexType{};
        IndexType operator*() const { return index; }
        explicit operator bool() const { return index != NULL_INDEX; }
        IndexType index{ NULL_INDEX };
    };

    Slotmap()
    {
        // for null object
        slots.allocate();
        data.emplace_back();
    }

    UserType& at(Index index) { return data[*index]; }
    const UserType& at(Index index) const { return data[*index]; }
    UserType& at(IndexType index) { return at(Index{ index }); }
    const UserType& at(IndexType index) const { return at(Index{ index }); }

    template <typename... Args> Index insert(Args&&... args)
    {
        const auto slot = slots.allocate();
        if(slot == ~IndexType{})
        {
            ENG_ASSERT(false); // no free slots, increase indextype size
            return Index{};
        }
        if(slot < data.size()) { data[slot] = UserType{ std::forward<Args>(args)... }; }
        else
        {
            ENG_ASSERT(slot == data.size());
            data.emplace_back(std::forward<Args>(args)...);
        }
        return Index{ slot };
    }

    void erase(Index index)
    {
        if(!index)
        {
            ENG_ASSERT(false); // someone has invalid index
            return;
        }
        if(!slots.erase(*index))
        {
            ENG_ASSERT(false); // someone had stale index
            return;
        }
        data[*index].~UserType();
    }

    void erase(IndexType index) { erase(Index{ index }); }

  private:
    SlotAllocator<IndexType> slots;
    std::vector<UserType> data;
};