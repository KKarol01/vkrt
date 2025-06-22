#pragma once

#include <concepts>
#include <cassert>
#include <eng/common/handle.hpp>
#include "sparseset.hpp"

template <typename T> class SparseMap
{
  public:
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto size() const { return set.size(); }

    T& at(Handle<T> slot) { return data.at(*slot); }
    T& operator[](Handle<T> slot) { return data[*slot]; }

    template <typename... Args>
    size_t emplace(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        const auto it = set.insert();
        if(it.index() < data.size()) { data.at(it.index()) = T{ std::forward<Args>(args)... }; }
        else if(it.index() == data.size()) { data.emplace_back(std::forward<Args>(args)...); }
        else { assert(false); }
        return it.index();
    }

    void erase(size_t slot)
    {
        const auto it = set.erase(slot);
        if(!it.valid) { return; }
        std::swap(at(slot), at(it.index()));
        at(it.index).~T();
    }

  private:
    SparseSet set;
    std::vector<T> data;
};
