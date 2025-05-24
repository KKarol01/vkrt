#pragma once

#include <span>
#include <array>
#include <vector>
#include <cstdint>
#include <concepts>
#include <cassert>

template <std::integral T = uint32_t> class SparseSet
{
    using page_t = T*;
    inline static constexpr size_t PAGE_SIZE = 4096;

  public:
    struct Iterator
    {
        explicit operator bool() const noexcept { return valid; }
        size_t dense_idx{};
        bool valid{ false };
    };

    /**
    Returns iterator with index to dense array. Iterator is always valid, however valid bool flag tells whether insertion actually took place.
    */
    Iterator insert(T e)
    {
        if(has(e)) { return make_iterator(e, false); }
        maybe_resize(e);
        get_sparse(e) = free_list_head;
        if(dense.size() <= free_list_head)
        {
            assert(free_list_head - dense.size() <= 1);
            dense.push_back(e);
        }
        else { dense.at(free_list_head) = e; }
        ++free_list_head;
        return make_iterator(e, true);
    }

    Iterator insert()
    {
        if(free_list_head < dense.size()) { return insert(dense.at(free_list_head)); }
        return insert(free_list_head);
    }

    bool has(T e) const
    {
        return get_page_index(e) < sparse.size() && sparse.at(get_page_index(e)) && get_sparse(e) < size() && extract(e) == e;
    }

    void erase(T e)
    {
        if(!has(e)) { return; }
        const auto idx = get_sparse(e);
        std::swap(dense.at(idx), dense.at(--free_list_head));
        get_sparse(dense.at(idx)) = idx;
    }

    /**
    Returns iterator with index to dense array. Iterator is valid only if the set contains the key.
    */
    Iterator get(T e) const
    {
        if(has(e)) { return make_iterator(e, true); }
        return Iterator{ e, false };
    }

    size_t size() const { return free_list_head; }

    T get_dense(Iterator it) const { return dense.at(it.dense_idx); }
    T get_dense(size_t idx) const { return dense.at(idx); }
    size_t get_dense_capacity() const { return dense.capacity(); }

  private:
    T& get_sparse(T e) { return sparse.at(get_page_index(e))[get_in_page_index(e)]; }
    const T& get_sparse(T e) const { return sparse.at(get_page_index(e))[get_in_page_index(e)]; }

    T& extract(T e) { return dense.at(get_sparse(e)); }
    const T& extract(T e) const { return dense.at(get_sparse(e)); }

    size_t get_page_index(T e) const { return e / PAGE_SIZE; }
    size_t get_in_page_index(T e) const { return e % PAGE_SIZE; }

    Iterator make_iterator(T e, bool is_valid) const { return Iterator{ get_sparse(e), is_valid }; }

    void maybe_resize(T e)
    {
        if(sparse.size() <= get_page_index(e) || !sparse.at(get_page_index(e)))
        {
            const auto page = get_page_index(e);
            if(sparse.size() <= page) { sparse.resize(page + 1); }
            sparse.at(page) = new T[PAGE_SIZE];
            assert(sparse.at(page));
        }
        if(dense.size() <= free_list_head)
        {
            const size_t ns = (size_t)((double)dense.size() * 1.5);
            dense.reserve(std::max((size_t)(free_list_head + 1), ns));
        }
    }

    std::vector<page_t> sparse;
    std::vector<T> dense;
    size_t free_list_head{};
};
