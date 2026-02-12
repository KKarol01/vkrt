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
        Index& as_index() { return std::get<0>(data); }
        UserType& as_user() { return std::get<1>(data); }
        const Index& as_index() const { return std::get<0>(data); }
        const UserType& as_user() const { return std::get<1>(data); }
        bool is_index() const { return data.index() == 0; }
        std::variant<Index, UserType> data;
    };

    using Page = Storage*;

  public:
    UserType& at(Index index)
    {
        auto& storage = get_storage(index);
        if(storage.is_index())
        {
            ENG_ASSERT(false);
            return at(Index{});
        }
        return storage.as_user();
    }
    const UserType& at(Index index) const
    {
        auto& storage = get_storage(index);
        if(storage.is_index())
        {
            ENG_ASSERT(false);
            return at(Index{});
        }
        return storage.as_user();
    }
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
        ENG_ASSERT(storage.is_index());
        free = storage.as_index();
        storage.data = UserType{ std::forward<Args>(args)... };
        return index;
    }

    void erase(Index index)
    {
        if(!index) { return; }
        auto& storage = get_storage(index);
        if(storage.is_index())
        {
            ENG_ASSERT(false);
            return;
        }
        storage.data = free;
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
        pages.push_back(new Storage[PAGE_SIZE]); // allocate aligned storage
        const auto page_index = pages.size() - 1;
        auto& page = pages.back();
        for(auto i = 0ull; i < PAGE_SIZE - 1; ++i)
        {
            page[i].data = make_index(page_index, i + 1); // construct a chain of free list from first element to the last
        }
        page[PAGE_SIZE - 1].data = free; // put current free in the last element
        free = page[0].as_index();       // make first element of this page current free
        if(page_index == 0) { page[0].data = UserType{}; }
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