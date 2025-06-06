#pragma once

#include <cstdint>
#include <compare>
#include <atomic>

template <typename T, typename Storage = uint32_t> struct Handle;

template <typename T, typename Storage> struct HandleGenerator {
    static constexpr Storage s_max_counter = ~Storage{};
    static Storage gen() {
        auto ct = counter.load(std::memory_order_relaxed);
        while(true) {
            if(ct == s_max_counter) {
                assert(false);
                return ct;
            }
            if(counter.compare_exchange_weak(ct, ct + 1, std::memory_order_relaxed)) { return ct; }
        }
    }
    inline static std::atomic<Storage> counter{ 0 };
};

struct HandleGenerate_T {};
inline constexpr HandleGenerate_T generate_handle{};

template <typename T, typename Storage> struct Handle {
    using Storage_T = Storage;
    constexpr Handle() = default;
    constexpr explicit Handle(Storage handle) : handle{ handle } {}
    explicit Handle(HandleGenerate_T) : handle{ HandleGenerator<T, Storage>::gen() } {}
    constexpr Storage operator*() const { return handle; }
    constexpr auto operator<=>(const Handle& h) const = default;
    constexpr explicit operator bool() const { return handle != ~Storage{}; }
    Storage handle{ ~Storage{} };
};

namespace std {
template <typename T> class hash<Handle<T>> {
  public:
    size_t operator()(const Handle<T>& h) const { return *h; }
};
} // namespace std