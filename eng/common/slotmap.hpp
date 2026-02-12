#pragma once

#include <cstdint>
#include <vector>
#include <new>
#include <memory>
#include <eng/common/logger.hpp>

template <typename UserType, size_t PAGE_SIZE, typename IndexType = uint32_t> class Slotmap
{
  public:
    struct Index
    {
        inline static constexpr IndexType NULL_INDEX = IndexType{};
        IndexType operator*() const { return index; }
        explicit operator bool() const { return index != NULL_INDEX; }
        IndexType index{ NULL_INDEX };
    };

  private:
    struct Storage
    {
        union {
            UserType data;
            Index next;
        };
    };

    using Page = Storage*;

  public:
    UserType& at(Index index) { return get_storage(index).data; }
    const UserType& at(Index index) const { return get_storage(index).data; }
    UserType& at(IndexType index) { return at(Index{ index }); }
    const UserType& at(IndexType index) const { return at(Index{ index }); }

    template <typename... Args> Index insert(Args&&... args)
    {
        // no free spot, try to allocate new page
        if(!free) { add_page(); }
        // could not allocate new page, possibly out of memory, returning index to null object
        if(!free)
        {
            ENG_ASSERT(false);
            return Index{};
        }
        const auto index = free;
        auto& storage = get_storage(index);
        free = storage.next;
        std::construct_at(&storage.data, std::forward<Args>(args)...);
        return index;
    }

    void erase(Index index)
    {
        if(!index) { return; }
        auto& storage = get_storage(index);
        storage.data.~UserType();
        std::construct_at(&storage.next, free);
        free = index;
    }

    void erase(IndexType index) { erase(Index{ index }); }

  private:
    static Index make_index(size_t page_index, size_t elem_index)
    {
        const auto idx = page_index * PAGE_SIZE + elem_index;
        if(idx > ~IndexType{})
        {
            ENG_ASSERT(false);
            return Index{};
        }
        return Index{ (IndexType)idx };
    }

    static void unpack_index(Index index, IndexType& page, IndexType& element)
    {
        page = *index / PAGE_SIZE;
        element = *index % PAGE_SIZE;
    }

    void add_page()
    {
        static constexpr size_t max_pages = ~IndexType{} / PAGE_SIZE;
        // check if our index can even reach the new page (out of range)
        // if indextype is u8, we can index max [0, 255], so 256 elements, then if our pagesize is 8,
        // page number 32 will have 256th element, so the last one we can index.
        if(pages.size() == max_pages) { return; }
        pages.push_back(reinterpret_cast<Storage*>(::operator new(sizeof(UserType) * PAGE_SIZE,
                                                                  std::align_val_t{ alignof(UserType) }))); // allocate aligned storage
        const auto page_index = pages.size() - 1;
        auto& page = pages.back();
        for(auto i = 0ull; i < PAGE_SIZE - 1; ++i)
        {
            std::construct_at(&page[i].next, make_index(page_index, i + 1)); // construct a chain of free list from first element to the last
        }
        std::construct_at(&page[PAGE_SIZE - 1].next, free); // put current free in the last element
        free = page[0].next;                                // make first element of this page current free
    }

    Storage& get_storage(Index index)
    {
        IndexType page;
        IndexType element;
        unpack_index(index, page, element);
        if(pages.size() <= page || PAGE_SIZE <= element)
        {
            ENG_ASSERT(false);
            return get_storage(Index{});
        }
        return pages[page][element];
    }

    const Storage& get_storage(Index index) const
    {
        IndexType page;
        IndexType element;
        unpack_index(index, page, element);
        if(pages.size() <= page || PAGE_SIZE <= element)
        {
            ENG_ASSERT(false);
            return get_storage(Index{});
        }
        return pages[page][element];
    }

    std::vector<Page> pages;
    Index free;
};