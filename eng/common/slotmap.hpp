#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <eng/common/types.hpp>

namespace eng
{

/*
  Thread-safe lockless object pool with a free list.
 */
template <typename UserType, usize PAGE_SIZE = 1024, usize NUM_PAGES = 64> class Slotmap
{
  public:
    using Storage = u32;
    using PageIndex = u32;
    using HeadTag = u32;
    using SlotId = TypedId<UserType, Storage>;
    inline static constexpr auto MAX_ELEMENTS = ~Storage{};

  private:
    struct Slot
    {
        union {
            UserType data;
            PageIndex next_free;
        };
    };
    struct Head
    {
        operator bool() const { return index != ~0u; }
        PageIndex index{ ~0u };
        HeadTag tag{};
    };

  public:
    UserType& at(SlotId slot)
    {
        if(!slot) { return null_object; }
        return _at(*slot).data;
    }

    const UserType& at(SlotId slot) const
    {
        if(!slot) { return null_object; }
        return _at(*slot).data;
    }

    auto& operator[](this auto& self, Storage idx) { return self.at(SlotId{ idx }); }

    template <typename... Args> SlotId emplace(Args&&... args)
    {
        const auto index = allocate();
        auto [page_index, array_index] = unpack_index(*index);
        if(page_index >= NUM_PAGES)
        {
            ENG_ASSERT(false, "Slotmap too small");
            return SlotId{};
        }
        auto* sp = slots[page_index].load();
        if(!sp)
        {
            std::allocator<Slot> allocator;
            Slot* new_page = allocator.allocate(PAGE_SIZE);
            Slot* expected = nullptr;
            if(!slots[page_index].compare_exchange_strong(expected, new_page))
            {
                allocator.deallocate(new_page, PAGE_SIZE);
                sp = expected;
            }
            else { sp = new_page; }
        }
        ENG_ASSERT(sp);
        std::construct_at<UserType>(&sp[array_index].data, std::forward<Args>(args)...);
        return index;
    }

    void erase(Storage index) { erase(SlotId{ index }); }

    void erase(SlotId index)
    {
        auto [page_index, array_index] = unpack_index(*index);
        if(page_index >= NUM_PAGES) { return; }
        auto* sp = slots[page_index].load();
        if(!sp) { return; }
        std::destroy_at(&sp[array_index].data);
        auto head = free_list.load();
        Head new_head;
        do
        {
            new_head.index = *index;
            new_head.tag = head.tag + 1;
            slots[page_index][array_index].next_free = head.index;
        }
        while(free_list.compare_exchange_weak(head, new_head));
    }

  private:
    static std::tuple<PageIndex, PageIndex> unpack_index(PageIndex index)
    {
        return std::make_tuple(index / PAGE_SIZE, index % PAGE_SIZE);
    }

    static PageIndex pack_index(PageIndex page, PageIndex slot) { return page * PAGE_SIZE + slot; }

    auto& _at(this auto& self, PageIndex index)
    {
        auto [page, slot] = unpack_index(index);
        return std::forward_like<decltype(self)>(self.slots[page][slot]);
    }

    SlotId allocate()
    {
        auto head = free_list.load();
        while(head)
        {
            Head new_head{ .index = _at(head.index).next_free, .tag = head.tag + 1 };
            if(free_list.compare_exchange_weak(head, new_head)) { return SlotId{ head.index }; }
        }
        return SlotId{ size.fetch_add(1) };
    }

    inline static UserType null_object{};
    std::atomic<Head> free_list{};
    std::atomic<Storage> size{};
    std::array<std::atomic<Slot*>, NUM_PAGES> slots{};
};

} // namespace eng