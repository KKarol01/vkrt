#pragma once

#include <eng/common/handle.hpp>
#include <unordered_map>
#include <cstdint>

//template <typename T, typename Storage = uint32_t> using HandleMap = std::unordered_map<Handle<T, Storage>, T>;

template <typename T, typename Storage = uint32_t> class HandleMap {
  public:
    using handle_t = Handle<T, Storage>;

    bool has(handle_t h) const { return storage.find(h) != storage.end(); }

    handle_t insert(const T& t) {
        const auto handle = handle_t{ generate_handle };
        if(handle) { storage.emplace(handle, t); }
        return handle;
    }

    handle_t insert(T&& t) {
        const auto handle = handle_t{ generate_handle };
        if(handle) { storage.emplace(handle, std::move(t)); }
        return handle;
    }

    handle_t emplace() { return insert(T{}); }

    T& at(handle_t h) { return storage.at(h); }
    const T& at(handle_t h) const { return storage.at(h); }

  private:
    std::unordered_map<handle_t, T> storage;
};
