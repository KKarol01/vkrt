#pragma once

#include <eng/common/handle.hpp>
#include <span>
#include <vector>
#include <cstdint>

template <typename T = uint32_t> class SparseSet {
  public:
    static constexpr T MAX_KEY = ~T{};

    struct Iterator {
        const T* key{};
        size_t dense_idx{};
    };

    Iterator insert(T e) {
        if(e == MAX_KEY) { return Iterator{}; }
        if(has(e)) { return make_iterator(e); }
        maybe_resize_sparse(e);
        sparse.at(e) = free_list_head;
        dense.insert(dense.begin() + free_list_head, e);
        ++free_list_head;
        return make_iterator(e);
    }

    Iterator insert() {
        Iterator it{};
        size_t cur = free_list_head;
        while(cur < dense.size()) {
            const auto val = dense.at(cur);
            if(has(val)) {
                ++cur;
                continue;
            }
            dense.erase(dense.begin() + free_list_head, dense.begin() + cur);
            assert(dense.at(free_list_head) == val);
            sparse.at(val) = free_list_head;
            ++free_list_head;
            return make_iterator(val);
        }
        if(cur != free_list_head) { dense.erase(dense.begin() + free_list_head, dense.end()); }
        return insert(free_list_head);
    }

    bool has(T e) const {
        return e < MAX_KEY && sparse.size() > e && free_list_head > sparse.at(e) && dense.at(sparse.at(e)) == e;
    }

    void destroy(T e) {
        if(!has(e)) { return; }
        const auto idx = sparse.at(e);
        std::swap(dense.at(idx), dense.at(--free_list_head));
        sparse.at(dense.at(idx)) = idx;
    }

    Iterator get(T e) const { return make_iterator(e); }

    size_t size() const { return dense.size(); }

    std::span<const T> get_dense() const { return std::span{ dense.begin(), dense.end() }; }

  private:
    Iterator make_iterator(T e) const {
        const auto* ptr = &dense.at(sparse.at(e));
        return { ptr, static_cast<size_t>(ptr - dense.data()) };
    }
    void maybe_resize_sparse(T e) {
        if(sparse.size() <= e) { sparse.resize(e + 1); }
    }

    std::vector<T> sparse;
    std::vector<T> dense;
    size_t free_list_head{};
};