#pragma once

#include <vector>
#include <algorithm>
#include <type_traits>
#include "handle.hpp"

template <typename T, typename Storage = uint32_t> class HandleVector {
  public:
    using Handle = Handle<T, Storage>;

    constexpr auto& back() { return storage.back(); }
    constexpr auto begin() { return storage.begin(); }
    constexpr const auto& back() const { return storage.back(); }
    constexpr auto cbegin() { return storage.cbegin(); }
    constexpr auto cend() { return storage.cend(); }
    constexpr auto empty() const { return storage.empty(); }
    constexpr const auto& front() const { return storage.front(); }
    constexpr auto end() { return storage.end(); }
    constexpr auto& front() { return storage.front(); }
    constexpr auto size() const { return storage.size(); }
    constexpr auto& at(size_t idx) { return storage.at(idx); }
    constexpr auto& at(Handle handle) { return at(find_idx(handle)); }
    constexpr auto& operator[](size_t idx) { return storage.at(idx); }

    template <typename K> constexpr Handle insert(K&& t) {
        if(std::is_constant_evaluated()) {
            const Handle h{ static_cast<Storage>(handles.size()) };
            const auto it = find_insertion_it(h);
            const auto idx = std::distance(handles.begin(), it);
            handles.insert(it, h);
            storage.insert(storage.begin() + idx, std::forward<T>(t));
            return h;
        } else {
            const Handle h{ generate_handle };
            const auto it = find_insertion_it(h);
            const auto idx = std::distance(handles.begin(), it);
            handles.insert(it, h);
            storage.insert(storage.begin() + idx, std::forward<K>(t));
            return h;
        }
    }
    template <typename... ARGS> constexpr Handle emplace(ARGS&&... args) {
        const Handle h{ generate_handle };
        const auto it = find_insertion_it(h);
        const auto idx = std::distance(handles.begin(), it);
        handles.insert(it, h);
        storage.emplace(storage.begin() + idx, std::forward<ARGS>(args)...);
        return h;
    }
    constexpr void erase(const T& t) { storage.erase(t); }
    constexpr T* try_find(Handle handle) {
        auto it = find_handle_it(handle);
        if(it == handles.end()) { return nullptr; }
        return &at(std::distance(handles.begin(), it));
    }
    constexpr size_t find_idx(Handle handle) { return std::distance(handles.begin(), find_handle_it(handle)); }
    constexpr Handle handle_at(size_t idx) const { return handles.at(idx); }

  private:
    constexpr auto find_handle_it(Handle h) {
        auto it = std::lower_bound(handles.begin(), handles.end(), h);
        if(*it != h) { return handles.end(); }
        return it;
    }
    constexpr auto find_insertion_it(Handle h) { return std::upper_bound(handles.begin(), handles.end(), h); }

    std::vector<Handle> handles;
    std::vector<T> storage;
};