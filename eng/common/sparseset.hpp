#pragma once

#include <vector>
#include <bit>
#include <numeric>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <memory>

template <typename IndexType = uint32_t, size_t PAGE_SIZE = 4096> class SparseSet
{
  public:
    using index_t = IndexType;
    using page_t = std::unique_ptr<index_t>;
    inline static constexpr index_t MAX_INDEX = ~index_t{};

    struct Iterator
    {
        explicit operator bool() const { return valid; }
        index_t operator*() const { return dense; }
        index_t dense{ ~index_t{} };  // Index to dense.
        index_t sparse{ ~index_t{} }; // Index to sparse. (stable)
        bool valid{ false };          // Was find, insertion or deletion successful.
    };

    auto begin() { return dense_array.begin(); }
    auto end() { return dense_array.begin() + next_free; }
    auto begin() const { return dense_array.begin(); }
    auto end() const { return dense_array.begin() + next_free; }

    // indexes dense.
    index_t at(size_t dense) const { return dense_array.at(dense); }

    bool has(index_t sparse) const
    {
        const auto pi = get_page_index(sparse);
        const auto ipi = get_in_page_index(sparse);
        if(sparse_array.size() <= pi || !sparse_array.at(get_page_index(sparse))) { return false; }
        const auto s = sparse_array.at(pi).get()[ipi];
        return s < size() && dense_array.at(s) == sparse;
    }

    size_t size() const { return next_free; }

    // extracts key from dense. may return ~key_t on invalid iterator.
    index_t get(Iterator it) const
    {
        if(!it || size() <= it.dense) { return ~index_t{}; }
        return dense_array.at(it.dense);
    }

    // extracts from sparse. is false when sparse set does not have the key.
    Iterator get(index_t sparse) const
    {
        if(has(sparse)) { return make_iterator(sparse, true); }
        return Iterator{};
    }

    // inserts new key. iterator is false if no insertion happened.
    Iterator insert(index_t sparse)
    {
        if(has(sparse)) { return make_iterator(sparse, false); }
        if(next_free > MAX_INDEX)
        {
            assert(false && "Too many indices");
            return make_iterator(MAX_INDEX, false);
        }

        // get page and allocate new if page is full
        const auto page = get_page_index(sparse);
        if(sparse_array.size() <= page) { sparse_array.resize(page + 1); }
        if(!sparse_array.at(page))
        {
            sparse_array.at(page).reset(new index_t[PAGE_SIZE]);
            memset(&sparse_array[page].get()[0], 0, sizeof(index_t) * PAGE_SIZE);
        }

        // insert element into dense. use free list head as index.
        // if some elements were removed via std::swap, reuse them
        assert(next_free <= dense_array.size());
        sparse_to_dense(sparse) = next_free;
        if(dense_array.size() == next_free) { dense_array.push_back(sparse); }
        else { dense_array.at(next_free) = sparse; }

        ++next_free;
        return make_iterator(sparse, true);
    }

    // generates unique value and inserts it. useful for entity creation
    Iterator insert()
    {
        if(next_free > MAX_INDEX)
        {
            assert(false && "Too many indices");
            return make_iterator(MAX_INDEX, false);
        }
        if(next_free == dense_array.size()) { dense_array.push_back(next_free); }
        return insert(dense_array.at(next_free));
    }

    // Returns index to dense at which deletion (if any) happened.
    // This is mainly used to delete from secondary vectors that keep associated data.
    Iterator erase(index_t sparse)
    {
        if(!has(sparse)) { return Iterator{}; }
        const auto idx = sparse_to_dense(sparse);
        std::swap(dense_array.at(idx), dense_array.at(--next_free)); // swap to reuse on later inserts
        sparse_to_dense(dense_array.at(idx)) = idx; // update the sparse index of the last element that got moved to new position
        return Iterator{ idx, sparse, true };
    }

  private:
    const index_t& sparse_to_dense(index_t sparse) const
    {
        return sparse_array.at(get_page_index(sparse)).get()[get_in_page_index(sparse)];
    }
    index_t& sparse_to_dense(index_t sparse)
    {
        return sparse_array.at(get_page_index(sparse)).get()[get_in_page_index(sparse)];
    }

    size_t get_page_index(index_t sparse) const { return sparse / PAGE_SIZE; }
    size_t get_in_page_index(index_t sparse) const { return sparse % PAGE_SIZE; }

    Iterator make_iterator(index_t sparse, bool is_valid) const
    {
        return Iterator{ sparse_to_dense(sparse), sparse, is_valid };
    }

    std::vector<page_t> sparse_array; // map entry to index to key
    std::vector<index_t> dense_array; // keys
    size_t next_free{};               // index to dense
};