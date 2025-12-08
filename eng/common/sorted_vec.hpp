#pragma once

#include <algorithm>
#include <vector>

namespace eng
{
template <typename T, typename Less = std::less<T>> class SortedVector
{
  public:
    auto begin() { return storage.begin(); }
    auto end() { return storage.end(); }
    auto size() const { return storage.size(); }
    auto data() { return storage.data(); }
    auto& at(size_t idx) { return storage.at(idx); }
    const auto& at(size_t idx) const { return storage.at(idx); }
    auto& operator[](size_t idx) { return storage[idx]; }
    const auto& operator[](size_t idx) const { return storage[idx]; }

    template <typename Elem> size_t insert(Elem&& e)
    {
        auto it = std::lower_bound(begin(), end(), e, Less{});
        storage.insert(it, std::forward<Elem>(e));
        return it - begin();
    }
    template <typename... Args> size_t emplace(Args&&... args) { return insert(T{ std::forward<Args>(args)... }); }

    template <typename Elem, typename CompEq = std::equal_to<void>> T* find(const Elem& e, CompEq comp_eq = {})
    {
        auto it = std::lower_bound(begin(), end(), e, comp_eq);
        if(it == end() || !CompEq{}(*it, e)) { return nullptr; }
        // lower bound stops at e <= it
        // if passes only if e >= it, so only it == e satisfies.
        return &*it;
    }

  private:
    std::vector<T> storage;
};
} // namespace eng
