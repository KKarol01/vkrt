#include <vector>
#include <functional>
#include <type_traits>
#include <algorithm>
#include <iostream>

struct UniqueInsert_T;

template <typename DataType, typename InsertBehavior = void, typename Comp = std::less<>>
class SortedVector : public std::vector<DataType> {
    using Base = std::vector<DataType>;

  public:
    using Base::back;
    using Base::begin;
    using Base::cbegin;
    using Base::cend;
    using Base::empty;
    using Base::end;
    using Base::front;
    using Base::size;

    template <typename T> constexpr DataType& insert(T&& t) {
        if constexpr(std::is_same_v<InsertBehavior, UniqueInsert_T>) {
            const auto it = find_element_it(t);
            if(it != end() && are_equal(t, *it)) { return *it; }
        }
        return *Base::insert(get_insertion_it(t), std::forward<T>(t));
    }

    template <typename... ARGS> constexpr DataType& emplace(ARGS&&... args) {
        return insert(DataType{ std::forward<ARGS>(args)... });
    }

    constexpr void erase(const DataType& t) {
        const auto it = find_element_it(t);
        if(it != end() && are_equal(t, *it)) { Base::erase(it); }
    }

  private:
    constexpr auto find_element_it(const DataType& d, Comp comp = Comp{}) const {
        return std::lower_bound(begin(), end(), d, comp);
    }
    constexpr auto get_insertion_it(const DataType& d, Comp comp = Comp{}) const {
        return std::upper_bound(begin(), end(), d, comp);
    }
    constexpr auto are_equal(const DataType& a, const DataType& b, Comp comp = Comp{}) const {
        return !comp(a, b) && !comp(b, a);
    }
};

template <typename DataType, typename Comp = std::less<>>
using SortedVectorUnique = SortedVector<DataType, UniqueInsert_T, Comp>;