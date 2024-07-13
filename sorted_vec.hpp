#pragma once

#include <vector>
#include <functional>
#include <type_traits>
#include <algorithm>

struct UNIQUE_INSERT;
struct NON_UNIQUE_INSERT;
template <typename INSERT_BHV, typename DATA_TYPE, typename COMP_LESS = std::less<>> class SortedVector_TMPL {

  public:
    SortedVector_TMPL() = default;
    SortedVector_TMPL(const std::vector<DATA_TYPE>& d) { *this = d; }
    SortedVector_TMPL(std::vector<DATA_TYPE>&& d) { *this = std::move(d); }
    SortedVector_TMPL(const SortedVector_TMPL& d) { *this = d; }
    SortedVector_TMPL(SortedVector_TMPL&& d) { *this = std::move(d); }
    SortedVector_TMPL& operator=(const std::vector<DATA_TYPE>& d) {
        _data = d;
        std::sort(_data.begin(), _data.end(), COMP_LESS{});
        return *this;
    }
    SortedVector_TMPL& operator=(std::vector<DATA_TYPE>&& d) {
        _data = std::move(d);
        std::sort(_data.begin(), _data.end(), COMP_LESS{});
        return *this;
    }
    SortedVector_TMPL& operator=(const SortedVector_TMPL& other) {
        _data = other._data;
        return *this;
    }
    SortedVector_TMPL& operator=(SortedVector_TMPL&& other) {
        _data = std::move(other._data);
        return *this;
    }
    DATA_TYPE& operator[](std::size_t id) { return _data[id]; }
    const DATA_TYPE& operator[](std::size_t id) const { return _data.at(id); }

    constexpr DATA_TYPE& insert(const DATA_TYPE& d) {
        if constexpr(std::is_same_v<INSERT_BHV, UNIQUE_INSERT>) {
            auto f_it = find_element_idx(d);
            if(f_it != _data.end() && d == (*f_it)) { return *f_it; }
        }
        auto it = get_insertion_idx(d);
        return *_data.insert(it, d);
    }

    constexpr DATA_TYPE& insert(DATA_TYPE&& d) {
        if constexpr(std::is_same_v<INSERT_BHV, UNIQUE_INSERT>) {
            auto f_it = find_element_idx(d);
            if(f_it != _data.end() && d == (*f_it)) { return *f_it; }
        }
        auto it = get_insertion_idx(d);
        return *_data.emplace(it, std::move(d));
    }

    constexpr void remove(const DATA_TYPE& d) {
        auto it = find_element_idx(d);
        if(it != _data.end() && *it == d) { _data.erase(it); }
    }

    template <typename VAL, typename PRED = COMP_LESS> constexpr DATA_TYPE* try_find(VAL&& v, PRED p = COMP_LESS{}) {
        auto it = std::equal_range(_data.begin(), _data.end(), v, [&p](auto&& e, auto&& v) { return p(e, v); });

        if(it.first != it.second) { return &*it.first; }
        return nullptr;
    }
    template <typename VAL, typename PRED = COMP_LESS> constexpr DATA_TYPE* try_find(VAL&& v, PRED p = COMP_LESS{}) const {
        auto it = std::equal_range(_data.begin(), _data.end(), v, [&p](auto&& e, auto&& v) { return p(e, v); });

        if(it.first != it.second) { return &*it.first; }
        return nullptr;
    }
    template <typename VAL, typename PRED = COMP_LESS> constexpr bool contains(VAL&& v, PRED p = COMP_LESS{}) {
        return try_find(v, p) != _data.end();
    }

    template <typename VAL, typename PRED> constexpr void remove(VAL&& v, PRED p = PRED{}) {
        auto it = try_find(v, p);
        if(it != _data.end() && v == *it) { _data.erase(it); }
    }

    template <typename... ARGS> constexpr DATA_TYPE& emplace(ARGS&&... args) { return insert(DATA_TYPE{ std::forward<ARGS>(args)... }); }

    constexpr auto data() noexcept { return _data.data(); }
    constexpr auto data() const noexcept { return _data.data(); }
    constexpr auto begin() noexcept { return _data.begin(); }
    constexpr auto end() noexcept { return _data.end(); }
    constexpr auto cbegin() const noexcept { return _data.cbegin(); }
    constexpr auto cend() const noexcept { return _data.cend(); }
    constexpr auto size() const noexcept { return _data.size(); }

  private:
    constexpr auto find_element_idx(const DATA_TYPE& d, COMP_LESS comp = COMP_LESS{}) {
        return std::lower_bound(_data.begin(), _data.end(), d, comp);
    }
    constexpr auto get_insertion_idx(const DATA_TYPE& d, COMP_LESS comp = COMP_LESS{}) {
        return std::upper_bound(_data.begin(), _data.end(), d, comp);
    }

    std::vector<DATA_TYPE> _data;
};

template <typename DATA_TYPE, typename COMP_LESS = std::less<>>
using SortedVector = SortedVector_TMPL<NON_UNIQUE_INSERT, DATA_TYPE, COMP_LESS>;

template <typename DATA_TYPE, typename COMP_LESS = std::less<>>
using SortedVectorUnique = SortedVector_TMPL<UNIQUE_INSERT, DATA_TYPE, COMP_LESS>;