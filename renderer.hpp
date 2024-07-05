#pragma once

#include <type_traits>
#include "model_importer.hpp"
#include "handle.hpp"

template <typename T> struct Flags {
    using U = typename std::underlying_type_t<T>;

    constexpr Flags() = default;
    constexpr Flags(T t) noexcept : flags(static_cast<U>(t)) {}
    constexpr Flags(U t) noexcept : flags(t) {}

    friend constexpr Flags<T> operator| <>(Flags<T>, T);
    friend constexpr Flags<T> operator& <>(Flags<T>, T);

    constexpr operator bool() const { return flags > 0; }

  private:
    U flags{ 0 };
};

template <typename T> inline constexpr Flags<T> operator|(Flags<T> a, T b) { return a.flags | static_cast<Flags<T>::U>(b); }
template <typename T> inline constexpr Flags<T> operator&(Flags<T> a, T b) { return a.flags & static_cast<Flags<T>::U>(b); }

enum class BatchFlags : uint32_t { RAY_TRACED_BIT = 0x1 };

inline Flags<BatchFlags> operator|(BatchFlags a, BatchFlags b) { return Flags{ a } | b; }

struct BatchSettings {
    Flags<BatchFlags> flags;
    struct RayTracing {

    } rt;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void render() = 0;
    virtual void batch_model(ImportedModel& model, BatchSettings settings) = 0;
    // virtual Handle<A> build_blas() { return {}; }
};
