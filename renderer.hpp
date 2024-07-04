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

enum class ModelBatchFlags : uint32_t { RAY_TRACED = 1 << 0 };

inline Flags<ModelBatchFlags> operator|(ModelBatchFlags a, ModelBatchFlags b) { return Flags{ a } | b; }

struct BatchSettings {
    Flags<ModelBatchFlags> flags;
    struct RayTracing {

    } rt;
};

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void init() = 0;
    virtual void batch_model(ImportedModel& model, BatchSettings settings) = 0;
    // virtual Handle<A> build_blas() { return {}; }
};
