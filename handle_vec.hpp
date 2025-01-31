#pragma once

#include <vector>
#include <algorithm>
#include <type_traits>
#include "handle.hpp"

template <typename T, typename Storage = uint32_t> class HandleVector {
  public:
    using Handle = Handle<T, Storage>;

    constexpr auto& back() { return storage.back(); }
    constexpr auto begin(this auto&& self) { return self.storage.begin(); }
    constexpr const auto& back() const { return storage.back(); }
    constexpr auto cbegin() { return storage.cbegin(); }
    constexpr auto cend() { return storage.cend(); }
    constexpr auto empty() const { return storage.empty(); }
    constexpr const auto& front() const { return storage.front(); }
    constexpr auto end(this auto&& self) { return self.storage.end(); }
    constexpr auto& front() { return storage.front(); }
    constexpr auto size() const { return storage.size(); }
    constexpr auto& at(uint64_t idx) { return storage.at(idx); }
    constexpr const auto& at(uint64_t idx) const { return storage.at(idx); }
    constexpr auto& at(Handle handle) { return at(find_idx(handle)); }
    constexpr const auto& at(Handle handle) const { return at(find_idx(handle)); }
    constexpr auto& operator[](uint64_t idx) { return storage.at(idx); }
    constexpr const auto& operator[](uint64_t idx) const { return storage.at(idx); }
    constexpr const auto& operator[](Handle handle) const { return at(handle); }
    constexpr auto data() { return storage.data(); }
    constexpr auto& data_storage() { return storage; }

    constexpr void insert(Handle h, auto&& t) {
        const auto it = find_insertion_it(h);
        const auto idx = std::distance(handles.begin(), it);
        handles.insert(it, h);
        storage.insert(storage.begin() + idx, std::forward<decltype(t)>(t));
    }
    constexpr Handle insert(auto&& t) {
        Handle h;
        if(std::is_constant_evaluated()) {
            h = Handle{ static_cast<Storage>(handles.size()) };
        } else {
            h = Handle{ generate_handle };
        }
        insert(h, std::forward<decltype(t)>(t));
        return h;
    }
    template <typename... ARGS> constexpr Handle emplace(ARGS&&... args) {
        const Handle h{ generate_handle };
        const auto it = find_insertion_it(h);
        const auto idx = std::distance(handles.begin(), it);
        handles.insert(it, h);
        storage.emplace(storage.begin() + idx, std::forward<ARGS>(args)...);
        return h;
    }
    constexpr T* try_find(Handle handle) {
        auto it = find_handle_it(handle);
        if(it == handles.end()) { return nullptr; }
        return &at(std::distance(handles.begin(), it));
    }
    constexpr uint64_t find_idx(Handle handle) const { return std::distance(handles.begin(), find_handle_it(handle)); }
    constexpr Handle handle_at(uint64_t idx) const { return handles.at(idx); }

  private:
    constexpr auto find_handle_it(this auto&& self, Handle h) {
        auto it = std::lower_bound(self.handles.begin(), self.handles.end(), h);
        if(*it != h) { return self.handles.end(); }
        return it;
    }
    constexpr auto find_insertion_it(Handle h) { return std::upper_bound(handles.begin(), handles.end(), h); }

    std::vector<Handle> handles;
    std::vector<T> storage;
};