#pragma once

#include <cstdint>
#include <vector>

struct SlotIndex
{
    static SlotIndex init(uint64_t value) { return init((uint32_t)value, (uint32_t)(value >> 32)); }
    static SlotIndex init(uint32_t index, uint32_t version) { return SlotIndex{ index, version }; }
    inline static constexpr uint32_t NULL_INDEX = ~uint32_t{};
    inline static constexpr uint32_t MAX_INDEX = ~uint32_t{};
    uint64_t operator*() const { return ((uint64_t)version << 32) | (uint64_t)index; }
    explicit operator bool() const { return index != NULL_INDEX; }
    uint32_t index{ NULL_INDEX };
    uint32_t version{};
};

template <typename UserType, size_t PAGE_SIZE> class Slotmap
{
    struct Storage
    {
        union {
            UserType data;
            SlotIndex next;
        };
        uint32_t version : 31 {};
        uint32_t is_occupied : 1 {};
    };

    using Page = Storage*;

  public:
    UserType& at(SlotIndex index)
    {
        if(!index) { return null_object; }
        auto& storage = get_storage(index);
        if(storage.is_occupied == 0 || storage.version != index.version) { return null_object; }
        return storage.data;
    }

    const UserType& at(SlotIndex index) const
    {
        if(!index) { return null_object; }
        const auto& storage = get_storage(index);
        if(storage.is_occupied == 0 || storage.version != index.version) { return null_object; }
        return storage.data;
    }

    template <typename... Args> SlotIndex insert(Args&&... args)
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

    bool erase(SlotIndex index)
    {
        if(!index) { return false; }
        auto& storage = get_storage(index);
        if(storage.is_occupied == 0 || index.version != storage.version) { return false; }
        ++storage.version;
        ++index.version;
        storage.is_occupied = 0;
        storage.data.~UserType();
        storage.next = next;
        next = index;
        return true;
    }

    bool has(SlotIndex index) const
    {
        if(!index) { return false; }
        size_t page_index = index.index / PAGE_SIZE;
        if(page_index >= pages.size()) { return false; }
        const auto& storage = get_storage(index);
        return storage.is_occupied == 1 && storage.version == index.version;
    }

    void for_each(const auto& callback) {
        for(auto i=0ull;)
    }

  private:
    Storage& get_storage(SlotIndex index)
    {
        size_t page_index = index.index / PAGE_SIZE;
        size_t elem_index = index.index % PAGE_SIZE;
        return pages[page_index][elem_index];
    }

    const Storage& get_storage(SlotIndex index) const
    {
        size_t page_index = index.index / PAGE_SIZE;
        size_t elem_index = index.index % PAGE_SIZE;
        return pages[page_index][elem_index];
    }

    void add_page()
    {
        // if we already have max number of pages that our index can reach, we return
        if(pages.size() >= SlotIndex::MAX_INDEX / PAGE_SIZE) { return; }
        auto* const page =
            reinterpret_cast<Storage*>(::operator new(sizeof(Storage) * PAGE_SIZE, std::align_val_t{ alignof(Storage) }));
        if(!page) { return; } // oom?
        pages.push_back(page);
        const auto page_index = (uint32_t)pages.size() - 1;
        for(uint32_t i = 0u; i < PAGE_SIZE - 1; ++i)
        {
            // create free list chain from first element to the last
            page[i].next = SlotIndex::init(page_index * PAGE_SIZE + i + 1, 0);
            page[i].version = 0;
            page[i].is_occupied = 0;
        }
        // make last element point to current free list head
        page[PAGE_SIZE - 1].next = next;
        page[PAGE_SIZE - 1].version = 0;
        page[PAGE_SIZE - 1].is_occupied = 0;
        // set current free list head to the beginning of the link
        next = SlotIndex::init(page_index * PAGE_SIZE, 0);
    }

    inline static UserType null_object{};
    std::vector<Page> pages;
    SlotIndex next;
};