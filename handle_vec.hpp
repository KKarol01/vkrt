#pragma once

#include <vector>
#include <utility>
#include <algorithm>
#include "handle.hpp"
#include "sorted_vec.hpp"

template <typename T, typename Storage = uint32_t> class HandleVector {
    using Handle = Handle<T, Storage>;

    struct Data {
        constexpr auto operator<=>(const Data& a) const { return handle <=> a.handle; }
        constexpr friend auto operator<=>(const Data& a, Handle b) { return a.handle <=> b; }
        constexpr friend auto operator<=>(Handle a, const Data& b) { return a <=> b.handle; }

        Handle handle;
        T data;
    };

  public:
    class Iterator {
      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = Data;
        using difference_type = ptrdiff_t;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using reference = value_type&;

        Iterator() = default;
        Iterator(pointer ptr) : ptr{ ptr } {}
        T* operator->() { return &ptr->data; }
        const T* operator->() const { return &ptr->data; }
        T& operator*() const { return ptr->data; }
        T& operator[](int v) const { return (ptr + v)->data; }
        Iterator& operator++() {
            ptr++;
            return *this;
        }
        Iterator operator++(int) {
            Iterator it = *this;
            ptr++;
            return it;
        }
        Iterator& operator--() {
            ptr--;
            return *this;
        }
        Iterator operator--(int) {
            Iterator it = *this;
            ptr--;
            return it;
        }
        Iterator& operator+=(int v) {
            ptr += v;
            return *this;
        }
        Iterator& operator-=(int v) {
            ptr -= v;
            return *this;
        }
        friend difference_type operator-(Iterator a, Iterator b) { return a.ptr - b.ptr; }
        friend Iterator operator+(Iterator a, int b) { return a.ptr - b; }
        friend Iterator operator-(Iterator a, int b) { return a.ptr - b; }
        friend Iterator operator+(int a, Iterator b) { return a - b.ptr; }
        constexpr auto operator<=>(const Iterator& it) const = default;
        constexpr auto get_handle() const { return ptr->handle; }

      private:
        pointer ptr;
    };

    constexpr auto& back() { return storage.back().data; };
    constexpr const auto& back() const { return storage.back().data; };
    constexpr auto begin() { return Iterator{ storage.data() }; };
    constexpr auto cbegin() { return Iterator{ storage.cdata() }; };
    constexpr auto cend() { return Iterator{ storage.cdata() + storage.size() }; };
    constexpr auto empty() const { return storage.empty(); };
    constexpr auto end() { return Iterator{ storage.data() + storage.size() }; };
    constexpr auto& front() { return storage.front().data; };
    constexpr const auto& front() const { return storage.front().data; };
    constexpr auto size() const { return storage.size(); };
    constexpr auto& at(size_t idx) { return storage.at(idx).data; }
    constexpr auto& at(Handle handle) { return storage.try_find(handle)->data; }
    constexpr auto& operator[](size_t idx) { return (storage.data() + idx)->data; }

    template <typename T> constexpr Handle insert(T&& t) {
        return storage.insert(Data{ Handle{ generate_handle }, std::forward<T>(t) }).handle;
    }
    template <typename... ARGS> constexpr Handle emplace(ARGS&&... args) {
        return storage.insert(Data{ Handle{ generate_handle }, T{ std::forward<ARGS>(args)... } }).handle;
    }
    constexpr void erase(const T& t) { storage.erase(t); }
    constexpr T* try_find(Handle handle) {
        if(auto* ptr = storage.try_find(handle); ptr) {
            return &ptr->data;
        } else {
            return nullptr;
        }
    }
    constexpr size_t find_idx(Handle handle) { return storage.find_idx(handle); }
    constexpr Handle handle_at(size_t idx) const { return storage.at(idx).handle; }

  private:
    SortedVector<Data> storage;
};