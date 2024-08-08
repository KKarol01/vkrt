#pragma once

#include <vector>
#include <utility>
#include <algorithm>

#include "handle.hpp"

template <typename T> class HandleVector {
    using Storage = uint32_t;
    using Offset = int32_t;

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
    auto begin() { return Iterator{ data.data() }; }
    auto end() { return Iterator{ data.data() + data.size() }; }

    Handle<T, Storage> push_back(const T& t) {
        Handle<T, Storage> handle{ static_cast<Storage>(data.size()) };
        data.emplace_back(handle, t);
        offsets.push_back(0);
        return handle;
    }

    Handle<T, Storage> push_back(T&& t) {
        Handle<T, Storage> handle{ static_cast<Storage>(data.size()) };
        data.emplace_back(handle, std::move(t));
        offsets.push_back(0);
        return handle;
    }

    T& get(Handle<T> h) { return data.at(index_of(h)).second; }

    constexpr Offset index_of(Handle<T> h) const { return *h + offsets.at(*h); }

    constexpr T& at(Storage idx) { return data.at(idx).second; }
    constexpr const T& at(Storage idx) const { return data.at(idx).second; }
    constexpr bool empty() const { return data.empty(); }
    constexpr auto size() const { return data.size(); }
    template<class Self> constexpr auto& front(this Self& self) { return self.data.front().second; }
    template<class Self> constexpr auto& back(this Self& self) { return self.data.back().second; }

    template <typename SortFunc = std::less<>> void sort(SortFunc&& func = {}) {
        std::sort(data.begin(), data.end(), [&func](const auto& a, const auto& b) { return func(a.second, b.second); });
        for(Offset i = 0; i < data.size(); ++i) {
            const auto& [handle, elem] = data.at(i);
            offsets.at(*handle) = *handle - i;
        }
    }

  private:
    std::vector<std::pair<Handle<T>, T>> data;
    std::vector<Offset> offsets;
};
