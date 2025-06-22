#pragma once

#include <span>
#include <array>
#include <vector>
#include <cstdint>
#include <concepts>
#include <cassert>

class SparseSet
{
  public:
    using T = uint32_t;
    using tpage_t = T*;
    inline static constexpr size_t PAGE_SIZE = 4096;

    struct Iterator
    {
        explicit operator bool() const noexcept { return valid; }
        size_t index() const { return dense_idx; }
        size_t dense_idx{};
        bool valid{ false };
    };

    /**
    Returns valid iterator with insertion bool flag.
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
        return get_page_index(e) < sparse.size() && sparse.at(get_page_index(e)) && get_sparse(e) < size() && get_dense(e) == e;
    }

    /**
    Returns iterator to removed element after the swap and success bool flag.
    */
    Iterator erase(T e)
    {
        if(!has(e)) { return make_iterator(e, false); }
        const auto idx = get_sparse(e);                       // 1. get index to dense
        std::swap(dense.at(idx), dense.at(--free_list_head)); // 2. swap dense places with last valid
        get_sparse(dense.at(idx)) = idx; // 3. get last moved element sparse index and make it point to new dense position
        return Iterator{ free_list_head, true };
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

    T get_dense(Iterator it) const { return dense.at(it.index()); }
    T get_dense(size_t idx) const { return dense.at(idx); }
    size_t get_dense_capacity() const { return dense.capacity(); }

  private:
    T& get_sparse(T e) { return sparse.at(get_page_index(e))[get_in_page_index(e)]; }
    const T& get_sparse(T e) const { return sparse.at(get_page_index(e))[get_in_page_index(e)]; }

    size_t get_page_index(T e) const { return e / PAGE_SIZE; }
    size_t get_in_page_index(T e) const { return e % PAGE_SIZE; }

    Iterator make_iterator(T e, bool is_valid) const { return Iterator{ get_sparse(e), is_valid }; }

    void maybe_resize(T e)
    {
        const auto page = get_page_index(e);
        if(sparse.size() <= page || !sparse.at(page))
        {
            if(sparse.size() <= page) { sparse.resize(page + 1); }
            sparse.at(page) = new T[PAGE_SIZE];
            assert(sparse.at(page));
        }
        if(dense.size() <= free_list_head)
        {
            const auto ns = static_cast<size_t>(std::ceil((double)dense.size() * 1.5));
            dense.reserve(ns);
        }
    }

    std::vector<tpage_t> sparse; // indices to dense
    std::vector<T> dense;
    size_t free_list_head{}; // index to dense
};
