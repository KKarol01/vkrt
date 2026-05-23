#pragma once

#include <vector>
#include <bit>
#include <numeric>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <memory>

template <std::integral Index = uint32_t> class SparseSet
{
    inline static constexpr size_t IDX_PER_PAGE = 16384 / sizeof(Index);

  public:
    inline static constexpr Index INVALID = ~Index{};

    auto begin() { return m_dense_vec.begin(); }
    auto end() { return m_dense_vec.begin() + size(); }
    auto begin() const { return m_dense_vec.begin(); }
    auto end() const { return m_dense_vec.begin() + size(); }

    // Allocates identifier, and returns index to linear dense storage.
    Index allocate(Index index)
    {
        // check if sparse set is full
        if(size() == INVALID) { return INVALID; }
        const auto [pi, ei] = unpack_index(index);
        // allocate page, if needed
        if(pi >= m_sparse_vec.size()) { m_sparse_vec.resize(pi + 1); }
        auto*& p = m_sparse_vec[pi];
        if(!p) { p = new Index[IDX_PER_PAGE]{}; }
        assert(p);
        if(!p) { return INVALID; }
        if(has(index)) { return p[ei]; }
        const auto di = m_size++;
        // reserve storage
        if(di >= m_dense_vec.capacity()) { m_dense_vec.reserve(align_up2(di + 1, IDX_PER_PAGE)); }
        // assign to dense index to sparse
        if(di == m_dense_vec.size()) { m_dense_vec.push_back(index); }
        else { m_dense_vec[di] = index; }
        // assign to sparse index to dense
        p[ei] = di;
        return di;
    }

    // Allocates generated identifier, and returns index to linear dense storage.
    Index allocate()
    {
        if(m_size == m_dense_vec.size()) { m_dense_vec.push_back(m_size); }
        return allocate(m_dense_vec[m_size]);
    }

    // Frees index
    Index free(Index index)
    {
        if(!has(index)) { return INVALID; }
        auto [pi, ei] = unpack_index(index);
        const auto di = m_sparse_vec[pi][ei];
        // swap indices to sparse. now both sparse are invalid
        std::swap(m_dense_vec[di], m_dense_vec[--m_size]);
        // unpack index to sparse for the swapped element
        // di is the removed one, the --m_size is the last one that will fill it's
        // spot for the linear iteration to work.
        // swapping, so that parameterless allocate() may reuse it.
        std::tie(pi, ei) = unpack_index(m_dense_vec[di]);
        // update index to dense for the replaced element
        m_sparse_vec[pi][ei] = di;
        return di;
    }

    // Checks if index is allocated
    bool has(Index index) const
    {
        if(index == INVALID) { return false; }
        const auto [pi, ei] = unpack_index(index);
        if(pi >= m_sparse_vec.size() || ei >= IDX_PER_PAGE) { return false; }
        const auto* p = m_sparse_vec[pi];
        if(!p) { return false; }
        const auto di = p[ei];
        if(di >= size() || index != m_dense_vec[di]) { return false; }
        return true;
    }

    // Returns linear storage index to dense.
    Index to_dense(Index index) const
    {
        if(!has(index)) { return INVALID; }
        const auto [pi, ei] = unpack_index(index);
        return m_sparse_vec[pi][ei];
    }

    // Returns the count of allocated indices.
    size_t size() const { return m_size; }

  private:
    static size_t align_up2(size_t index, size_t alignment)
    {
        assert((alignment & (alignment - 1)) == 0);
        return (index + alignment - 1) & ~(alignment - 1);
    }
    static std::pair<Index, Index> unpack_index(Index index)
    {
        return std::make_pair(index / IDX_PER_PAGE, index % IDX_PER_PAGE);
    }

    std::vector<Index*> m_sparse_vec;
    std::vector<Index> m_dense_vec;
    Index m_size{};
};
