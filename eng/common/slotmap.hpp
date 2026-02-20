#pragma once

#include <cstdint>
#include <vector>
#include <eng/common/handle.hpp>

template <typename Storage> struct SlotIndex;
template <> struct SlotIndex<uint32_t> : public eng::TypedId<SlotIndex<uint32_t>, uint32_t>
{
    inline static constexpr uint32_t INDEX_BIT_MASK = 0x00FFFFFFu;
    inline static constexpr uint32_t MAX_INDEX = INDEX_BIT_MASK;
    using TypedId::TypedId;
    SlotIndex(uint32_t index, uint32_t version) : TypedId(((version & 0xFF) << 24) | (index & INDEX_BIT_MASK)) {}
    uint32_t get_index() const { return handle & INDEX_BIT_MASK; }
    uint32_t get_version() const { return (handle & ~INDEX_BIT_MASK) >> 24; }
    uint32_t inc_version()
    {
        const auto ver = (get_version() + 1) & 0xFF;
        handle = (ver << 24) | get_index();
        return ver;
    }
};

template <typename UserType, size_t PAGE_SIZE, typename IndexType = uint32_t> class Slotmap
{
  public:
    using SlotId = SlotIndex<IndexType>;

  private:
    struct Storage
    {
        union {
            UserType data;
            SlotId next;
        };
        uint32_t version : 31 {};
        uint32_t is_occupied : 1 {};
    };

    using Page = Storage*;

  public:
    UserType& at(SlotId index)
    {
        if(!has(index)) { return null_object; }
        return get_storage(index).data;
    }

    const UserType& at(SlotId index) const
    {
        if(!has(index)) { return null_object; }
        return get_storage(index).data;
    }

    template <typename... Args> SlotId insert(Args&&... args)
    {
        if(!next) { add_page(); }
        if(!next) { return next; }
        const auto index = next;
        auto& storage = get_storage(index);
        next = storage.next;
        std::construct_at<UserType>(&storage.data, std::forward<Args>(args)...);
        storage.is_occupied = 1;
        return index;
    }

    bool erase(SlotId index)
    {
        if(!has(index)) { return false; }
        auto& storage = get_storage(index);
        index.inc_version();
        storage.version = index.get_version();
        storage.is_occupied = 0;
        storage.data.~UserType();
        storage.next = next;
        next = index;
        return true;
    }

    bool has(SlotId index) const
    {
        if(!index) { return false; }
        size_t page_index = index.get_index() / PAGE_SIZE;
        if(page_index >= pages.size()) { return false; }
        const auto& storage = get_storage(index);
        return storage.is_occupied == 1 && storage.version == index.get_version();
    }

    void for_each(const auto& callback)
    {
        // for(auto i=0ull;)
    }

  private:
    Storage& get_storage(SlotId index)
    {
        size_t page_index = index.get_index() / PAGE_SIZE;
        size_t elem_index = index.get_index() % PAGE_SIZE;
        return pages[page_index][elem_index];
    }

    const Storage& get_storage(SlotId index) const
    {
        size_t page_index = index.get_index() / PAGE_SIZE;
        size_t elem_index = index.get_index() % PAGE_SIZE;
        return pages[page_index][elem_index];
    }

    void add_page()
    {
        // if we already have max number of pages that our index can reach, we return
        if(pages.size() >= SlotId::MAX_INDEX / PAGE_SIZE) { return; }
        auto* const page =
            reinterpret_cast<Storage*>(::operator new(sizeof(Storage) * PAGE_SIZE, std::align_val_t{ alignof(Storage) }));
        if(!page) { return; } // oom?
        pages.push_back(page);
        const auto page_index = (uint32_t)pages.size() - 1;
        for(uint32_t i = 0u; i < PAGE_SIZE - 1; ++i)
        {
            // create free list chain from first element to the last
            page[i].next = SlotId{ (uint32_t)(page_index * PAGE_SIZE + i + 1), 0 };
            page[i].version = 0;
            page[i].is_occupied = 0;
        }
        // make last element point to current free list head
        page[PAGE_SIZE - 1].next = next;
        page[PAGE_SIZE - 1].version = 0;
        page[PAGE_SIZE - 1].is_occupied = 0;
        // set current free list head to the beginning of the link
        next = SlotId{ (uint32_t)(page_index * PAGE_SIZE), 0 };
    }

    inline static UserType null_object{};
    std::vector<Page> pages;
    SlotId next;
};