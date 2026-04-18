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
template <typename UserType, size_t PAGE_SIZE = 1024, size_t NUM_PAGES = 64> class Slotmap
{
  public:
    using Storage = uint32_t;
    using SlotId = TypedId<UserType, Storage>;
    inline static constexpr auto MAX_ELEMENTS = ~Storage{};

  private:
    struct Slot
    {
        union {
            UserType data;
            Storage next_free;
        };
    };
    struct Head
    {
        operator bool() const { return index != ~0u; }
        uint32_t index{ ~0u };
        uint32_t tag{};
    };

  public:
    UserType& at(SlotId index)
    {
        if(!index) { return null_object; }
        Storage page_index, array_index;
        unpack_index(*index, page_index, array_index);
        return slots[page_index][array_index].data;
    }

    const UserType& at(SlotId index) const
    {
        if(!index) { return null_object; }
        Storage page_index, array_index;
        unpack_index(*index, page_index, array_index);
        return slots[page_index][array_index].data;
    }

    template <typename... Args> SlotId insert(Args&&... args)
    {
        const auto index = allocate();
        Storage page_index, array_index;
        unpack_index(*index, page_index, array_index);
        if(page_index >= NUM_PAGES) { return SlotId{}; }
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
        Storage page_index, array_index;
        unpack_index(*index, page_index, array_index);
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
    static void unpack_index(Storage index, Storage& out_page, Storage& out_slot)
    {
        out_page = index / PAGE_SIZE;
        out_slot = index % PAGE_SIZE;
    }

    static Storage pack_index(Storage page, Storage slot) { return page * PAGE_SIZE + slot; }

    SlotId allocate()
    {
        auto head = free_list.load();
        while(head)
        {
            Storage page_index, array_index;
            unpack_index(head.index, page_index, array_index);
            Head new_head{ .index = slots[page_index][array_index].next_free, .tag = head.tag + 1 };
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