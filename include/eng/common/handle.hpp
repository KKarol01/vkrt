#pragma once

#include <cstdint>
#include <compare>
#include <concepts>
#include <type_traits>
#include <eng/common/types.hpp>
#include <eng/common/logger.hpp>

namespace eng
{
// Defines how many of the 32 handle bits are used for slot index, for each type separately.
// The remaining bits store the generation of the slot, so that stale handles can be detected.
template <typename T> struct HandleTraits
{
    inline static constexpr u32 INDEX_BITS = 24;
};

// Specialized with ENG_HANDLE_POOL / ENG_HANDLE_CONST_POOL for each type that handle
// should be dereferenceable for. get() returns the Pool/UniquePool storing the objects.
template <typename T> struct HandlePool;

#define ENG_HANDLE_POOL(type, expr)                                                                                    \
    template <> struct HandlePool<type>                                                                                \
    {                                                                                                                  \
        static auto& get() { return expr; }                                                                            \
    }
#define ENG_HANDLE_CONST_POOL(type, expr)                                                                              \
    template <> struct HandlePool<type>                                                                                \
    {                                                                                                                  \
        static const auto& get() { return expr; }                                                                      \
    }

/*
  Typed, generation-checked index into a Pool. Behaves like a pointer:
  operator-> and get() go through HandlePool<T> and assert on null or stale handle.
  Obtained pointers and references stay valid until the object is erased.
 */
template <typename T> struct Handle : public TypedId<T, u32>
{
  public:
    inline static constexpr u32 INDEX_BITS = HandleTraits<T>::INDEX_BITS;
    inline static constexpr u32 GEN_BITS = 32u - INDEX_BITS;
    static_assert(INDEX_BITS > 0u && INDEX_BITS < 32u);
    inline static constexpr u32 INDEX_MASK = (1u << INDEX_BITS) - 1u;
    inline static constexpr u32 GEN_MASK = (1u << GEN_BITS) - 1u;

    using TypedId<T, u32>::TypedId;
    constexpr Handle(u32 index, u32 gen) : TypedId<T, u32>((index & INDEX_MASK) | ((gen & GEN_MASK) << INDEX_BITS)) {}

    constexpr u32 index() const { return this->handle & INDEX_MASK; }
    constexpr u32 generation() const { return (this->handle >> INDEX_BITS) & GEN_MASK; }

    // Returns nullptr when the handle is null or stale.
    auto* try_get() const { return HandlePool<T>::get().try_get(*this); }
    auto* operator->() const
    {
        auto* ptr = try_get();
        ENG_ASSERT(ptr, "Dereferencing null or stale handle.");
        return ptr;
    }
    auto& get() const { return *operator->(); }
};

} // namespace eng

namespace std
{

template <typename T> class hash<::eng::Handle<T>>
{
  public:
    size_t operator()(const ::eng::Handle<T>& h) const { return *h; }
};

} // namespace std
