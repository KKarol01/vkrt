#pragma once

#include <vector>
#include <functional>
#include <type_traits>
#include <algorithm>
#include <iostream>

struct UniqueInsert_T;

template <typename DataType, typename InsertBehavior = void, typename Comp = std::less<>> class SortedVector {
  public:
    constexpr auto& back() { return storage.back(); };
    constexpr const auto& back() const { return storage.back(); };
    constexpr auto begin() { return storage.begin(); };
    constexpr auto cbegin() const { return storage.cbegin(); };
    constexpr auto cend() const { return storage.cend(); };
    constexpr auto empty() const { return storage.empty(); };
    constexpr auto end() { return storage.end(); };
    constexpr auto& front() { return storage.front(); };
    constexpr const auto& front() const { return storage.front(); };
    constexpr auto size() const { return storage.size(); };
    constexpr auto data() { return storage.data(); };
    constexpr auto cdata() const { return storage.cdata(); };
    constexpr auto& at(size_t idx) { return storage.at(idx); }
    constexpr const auto& at(size_t idx) const { return storage.at(idx); }

    template <typename T> constexpr DataType& insert(T&& t) {
        if constexpr(std::is_same_v<InsertBehavior, UniqueInsert_T>) {
            const auto it = find_element_it(t);
            if(it != end() && are_equal(t, *it)) { return *it; }
        }
        return *storage.insert(get_insertion_it(t), std::forward<T>(t));
    }
    template <typename... ARGS> constexpr DataType& emplace(ARGS&&... args) {
        return insert(DataType{ std::forward<ARGS>(args)... });
    }
    constexpr void erase(const DataType& t) {
        const auto it = find_element_it(t);
        if(it != end() && are_equal(t, *it)) { storage.erase(it); }
    }
    template <typename T, typename CompFunc = Comp>
    constexpr const DataType* try_find(const T& t, CompFunc&& comp = CompFunc{}) const {
        if(auto it = try_find_it(t, std::forward<CompFunc>(comp)); it != cend()) { return &*it; }
        return nullptr;
    }
    template <typename T, typename CompFunc = Comp>
    constexpr DataType* try_find(const T& t, CompFunc&& comp = CompFunc{}) {
        if(auto it = try_find_it(t, std::forward<CompFunc>(comp)); it != end()) { return &*it; }
        return nullptr;
    }
    template <typename T, typename CompFunc = Comp>
    constexpr size_t find_idx(const T& t, CompFunc&& comp = CompFunc{}) const {
        return std::distance(cbegin(), try_find_it(t, std::forward<CompFunc>(comp)));
    }

  private:
    constexpr auto find_element_it(const DataType& d, Comp comp = Comp{}) {
        return std::lower_bound(begin(), end(), d, comp);
    }
    constexpr auto get_insertion_it(const DataType& d, Comp comp = Comp{}) {
        return std::upper_bound(begin(), end(), d, comp);
    }
    template <typename T> constexpr auto are_equal(const DataType& a, const T& b, Comp comp = Comp{}) const {
        return !comp(a, b) && !comp(b, a);
    }
    template <typename T, typename CompFunc = Comp>
    constexpr auto try_find_it(const T& t, CompFunc&& comp = CompFunc{}) {
        auto range = std::equal_range(begin(), end(), t, comp);
        for(auto it = range.first; it != range.second; ++it) {
            if(are_equal(*it, t)) { return it; }
        }
        return end();
    }
    template <typename T, typename CompFunc = Comp>
    constexpr auto try_find_it(const T& t, CompFunc&& comp = CompFunc{}) const {
        auto range = std::equal_range(cbegin(), cend(), t, comp);
        for(auto it = range.first; it != range.second; ++it) {
            if(are_equal(*it, t)) { return it; }
        }
        return cend();
    }

    std::vector<DataType> storage;
};

template <typename DataType, typename Comp = std::less<>>
using SortedVectorUnique = SortedVector<DataType, UniqueInsert_T, Comp>;