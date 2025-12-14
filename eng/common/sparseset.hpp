#pragma once

#include <vector>
#include <bit>
#include <numeric>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <memory>

class SparseSet
{
  public:
    inline static constexpr size_t PAGE_SIZE = 4096;
    using key_t = uint64_t;
    using page_t = std::unique_ptr<key_t>;
    using index_t = size_t;

    struct Iterator
    {
        explicit operator bool() const noexcept { return valid; }
        index_t index{ ~index_t{} }; // Index to dense.
        bool valid{ false };         // Was find, insertion or deletion successful.
    };

    auto begin() { return dense.begin(); }
    auto end() { return dense.begin() + free_list_head; }

    // indexes dense.
    key_t at(size_t idx) const { return dense.at(idx); }

    bool has(key_t e) const
    {
        const auto pi = get_page_index(e);
        const auto ipi = get_in_page_index(e);
        if(sparse.size() <= pi || !sparse.at(get_page_index(e))) { return false; }
        const auto s = sparse.at(pi).get()[ipi];
        return s < size() && dense.at(s) == e;
    }

    size_t size() const { return free_list_head; }

    // extracts key from dense. may return ~key_t on invalid iterator.
    key_t get(Iterator it) const
    {
        if(!it || size() <= it.index) { return ~key_t{}; }
        return dense.at(it.index);
    }

    // extracts from sparse. is false when sparse set does not have the key.
    Iterator get(key_t e) const
    {
        if(has(e)) { return make_iterator(e, true); }
        return Iterator{};
    }

    // inserts new key. iterator is false if no insertion happened.
    Iterator insert(key_t e)
    {
        if(has(e)) { return make_iterator(e, false); }

        // get page and allocate new if page is full
        const auto page = get_page_index(e);
        if(sparse.size() <= page) { sparse.resize(page + 1); }
        if(!sparse.at(page)) { sparse.at(page).reset(new key_t[PAGE_SIZE]); }

        // insert element into dense. use free list head as index.
        // if some elements were removed via std::swap, reuse them
        assert(free_list_head <= dense.size());
        get_sparse(e) = free_list_head;
        if(dense.size() == free_list_head) { dense.push_back(e); }
        else { dense.at(free_list_head) = e; }

        ++free_list_head;
        return make_iterator(e, true);
    }

    // generates unique value and inserts it. useful for entity creation
    Iterator insert()
    {
        if(free_list_head == dense.size()) { dense.push_back(free_list_head); }
        return insert(dense.at(free_list_head));
    }

    // Returns index to dense at which deletion (if any) happened.
    // This is mainly used to delete from secondary vectors that keep associated data.
    Iterator erase(key_t e)
    {
        if(!has(e)) { return Iterator{}; }
        const auto idx = get_sparse(e);
        std::swap(dense.at(idx), dense.at(--free_list_head)); // swap to reuse on later inserts
        get_sparse(dense.at(idx)) = idx; // update the sparse index of the last element that got moved to new position
        return Iterator{ idx, true };
    }

  private:
    key_t& get_sparse(key_t e) { return sparse.at(get_page_index(e)).get()[get_in_page_index(e)]; }
    const key_t& get_sparse(key_t e) const { return sparse.at(get_page_index(e)).get()[get_in_page_index(e)]; }

    size_t get_page_index(key_t e) const { return e / PAGE_SIZE; }
    size_t get_in_page_index(key_t e) const { return e % PAGE_SIZE; }

    Iterator make_iterator(key_t e, bool is_valid) const { return Iterator{ get_sparse(e), is_valid }; }

    std::vector<page_t> sparse; // map entry to index to key
    std::vector<key_t> dense;   // keys
    size_t free_list_head{};    // index to dense
};