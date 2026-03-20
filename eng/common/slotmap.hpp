#pragma once

#include <cstdint>
#include <vector>
#include <eng/common/types.hpp>

namespace eng
{

template <typename UserType, size_t PAGE_SIZE, typename Storage = uint32_t> class Slotmap
{
  public:
    using SlotId = TypedId<UserType, Storage>;
    inline static constexpr auto MAX_ELEMENTS = ~Storage{};

  private:
    struct Slot
    {
        bool has_next() const { return next_or_data.index() == 0; }
        SlotId& get_next() { return std::get<0>(next_or_data); }
        UserType& get_data() { return std::get<1>(next_or_data); }
        const UserType& get_data() const { return std::get<1>(next_or_data); }
        std::variant<SlotId, UserType> next_or_data;
    };

    using Page = Slot*;

  public:
    UserType& at(SlotId index)
    {
        if(!has(index)) { return null_object; }
        return get_slot(index).get_data();
    }

    const UserType& at(SlotId index) const
    {
        if(!has(index)) { return null_object; }
        return get_slot(index).get_data();
    }

    template <typename... Args> SlotId insert(Args&&... args)
    {
        if(!next)
        {
            if(!add_page()) { return SlotId{}; }
        }
        if(!next) { return next; }
        const auto index = next;
        auto& slot = get_slot(index);
        ENG_ASSERT(slot.has_next());
        next = slot.get_next();
        slot.next_or_data.emplace<UserType>(std::forward<Args>(args)...);
        return index;
    }

    bool erase(SlotId index)
    {
        if(!has(index)) { return false; }
        auto& slot = get_slot(index);
        slot.next_or_data = next;
        next = index;
        return true;
    }

    bool has(SlotId index) const
    {
        if(!index) { return false; }
        size_t page_index = *index / PAGE_SIZE;
        if(page_index >= pages.size()) { return false; }
        const auto& slot = get_slot(index);
        return !slot.has_next();
    }

    void for_each(const auto& callback)
    {
        static_assert(false);
        // for(auto i=0ull;)
    }

  private:
    Slot& get_slot(SlotId index)
    {
        size_t page_index = *index / PAGE_SIZE;
        size_t elem_index = *index % PAGE_SIZE;
        return pages[page_index][elem_index];
    }

    const Slot& get_slot(SlotId index) const
    {
        size_t page_index = *index / PAGE_SIZE;
        size_t elem_index = *index % PAGE_SIZE;
        return pages[page_index][elem_index];
    }

    bool add_page()
    {
        // if we already have max number of pages that our index can reach, we return
        if(pages.size() >= *SlotId{} / PAGE_SIZE) { return false; }
        std::allocator<Slot> pageallocator;
        auto* const page = pageallocator.allocate(PAGE_SIZE);
        if(!page) { return false; } // oom?
        pages.push_back(page);
        const auto page_index = (uint32_t)pages.size() - 1;
        for(uint32_t i = 0u; i < PAGE_SIZE - 1; ++i)
        {
            // create free list chain from first element to the last
            page[i].next_or_data = SlotId{ (uint32_t)(page_index * PAGE_SIZE + i + 1) };
        }
        // make last element point to current free list head
        page[PAGE_SIZE - 1].next_or_data = next;
        // set current free list head to the beginning of the link
        next = SlotId{ (uint32_t)(page_index * PAGE_SIZE) };
        return true;
    }

    inline static UserType null_object{};
    std::vector<Page> pages;
    SlotId next;
};

} // namespace eng
