#pragma once

#include <vector>
#include <utility>
#include <algorithm>

#include "handle.hpp"

template <typename T> class HandleVector {
    using Storage = uint32_t;

    struct Iterator : public std::iterator<std::forward_iterator_tag, T> {
        using Ref = T&;
        using Ptr = T*;
        using WrappedPtr = std::pair<Handle<T>, T>*;

        Iterator(WrappedPtr ptr) : ptr{ ptr } {}

        Ref operator*() { return ptr->second; }
        Ptr operator->() { return &ptr->second; }
        Iterator& operator++() {
            ++ptr;
            return *this;
        }
        Iterator& operator++(int) {
            Iterator temp = *this;
            ++(*this);
            return temp;
        }

        friend bool operator==(const Iterator& a, const Iterator& b) { return a.ptr == b.ptr; }
        friend bool operator!=(const Iterator& a, const Iterator& b) { return a.ptr != b.ptr; }

        WrappedPtr ptr;
    };

  public:
    constexpr auto begin() { return Iterator{ data.data() }; }
    constexpr auto end() { return Iterator{ data.data() + data.size() }; }

    template <class Data> constexpr Handle<T, Storage> push_back(Data&& d) {
        Handle<T, Storage> h = gen_handle();
        data.emplace_back(h, std::forward<Data>(d));
        data_indices[h] = static_cast<Storage>(data.size() - 1ull);
        return h;
    }

    T& get(Handle<T> h) { return data.at(index_of(h)).second; }
    const T& get(Handle<T> h) const { return data.at(index_of(h)).second; }

    constexpr Storage index_of(Handle<T, Storage> h) const { return data_indices.at(h); }

    constexpr T& at(Storage idx) { return data.at(idx).second; }
    constexpr const T& at(Storage idx) const { return data.at(idx).second; }
    constexpr bool empty() const { return data.empty(); }
    constexpr auto size() const { return data.size(); }
    template <class Self> constexpr auto& front(this Self& self) { return self.data.front().second; }
    template <class Self> constexpr auto& back(this Self& self) { return self.data.back().second; }

    template <typename SortFunc = std::less<>> void sort(SortFunc&& func = {}) {
        std::sort(data.begin(), data.end(), [&func](const auto& a, const auto& b) { return func(a.second, b.second); });
        for(Storage i = 0; i < data.size(); ++i) {
            const auto& [handle, elem] = data.at(i);
            data_indices[handle] = i;
        }
    }

  private:
    Handle<T, Storage> gen_handle() { return Handle<T, Storage>{ _handle++ }; }

    static inline Storage _handle{};
    std::vector<std::pair<Handle<T>, T>> data;
    std::unordered_map<Handle<T>, Storage> data_indices;
};