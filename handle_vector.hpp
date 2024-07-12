#pragma once

#include <vector>
#include <concepts>
#include "handle.hpp"

template <typename T, typename HandleStorage = std::uint32_t> struct HandleVector {
  public:
    template <typename PushedType> Handle<T, HandleStorage> insert(PushedType&& val) {
        auto free_handle = get_free_handle();
        if(*free_handle >= storage.capacity()) {
            storage.push_back(std::forward<PushedType>(val));
        } else {
            storage.at(*free_handle) = std::forward<PushedType>(val);
        }
        return free_handle;
    }

    template <typename PushedType, typename Iterator>
    std::vector<Handle<T, HandleStorage>> insert(PushedType&& val, Iterator begin, Iterator end) {
        std::vector<Handle<T, HandleStorage>> handles(std::distance(begin, end));
        for(auto i = 0ull; handles.size(); ++i) {
            handles.at(i) = Handle<T, HandleStorage>{ static_cast<HandleStorage>(storage.size() + i) };
        }

        storage.insert(storage.end(), begin, end);

        return handles;
    }

    T&& erase(Handle<T, HandleStorage> handle) {
        invalid_handles.push_back(handle);
        return std::move(storage.at(*handle));
    }

    T& get(Handle<T, HandleStorage> handle) { return storage.at(handle._handle); }
    const T& get(Handle<T, HandleStorage> handle) const { return storage.at(handle._handle); }

    auto empty() const { return storage.empty(); }
    auto size() const { return storage.size(); }
    auto& front() { return storage.front(); }
    auto& back() { return storage.back(); }
    auto begin() { return storage.begin(); }
    auto end() { return storage.end(); }
    auto& at(size_t idx) { return storage.at(idx); }
    auto& at(size_t idx) const { return storage.at(idx); }

  private:
    Handle<T, HandleStorage> get_free_handle() {
        Handle<T, HandleStorage> h;

        if(!invalid_handles.empty()) {
            h = Handle<T, HandleStorage>{ invalid_handles.back() };
            invalid_handles.erase(invalid_handles.end() - 1);
        } else {
            h = Handle<T, HandleStorage>{ static_cast<HandleStorage>(storage.size()) };
        }

        return h;
    }

    std::vector<T> storage;
    std::vector<Handle<T, HandleStorage>> invalid_handles;
};
