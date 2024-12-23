#pragma once
#include <atomic>
#include <array>
#include <cassert>
#include <span>
#include <memory>
#include <utility>
#include "handle.hpp"
#include "common/types.hpp"
#include "handle_vec.hpp"

template <typename Storage = uint32_t> struct EntityComponentIdGenerator {
    template <typename T> static Storage get_id() {
        static Storage idx = counter++;
        return idx;
    };
    inline static std::atomic<Storage> counter{ Storage{} };
};

struct ComponentArrayBase {
    virtual ~ComponentArrayBase() = default;
};

template <typename T, typename Storage = uint32_t> struct ComponentArray : public ComponentArrayBase {
    HandleVector<T, Storage> data;
};

class EntityComponents {
  public:
    inline static constexpr uint32_t MAX_COMPONENTS = 8; // components' ids get stored in uint32_t bitfield to indicate presence
    constexpr EntityComponents() = default;
    EntityComponents(const EntityComponents&) = delete;
    EntityComponents& operator=(const EntityComponents&) = delete;
    EntityComponents(EntityComponents&&) = delete;
    EntityComponents& operator=(EntityComponents&&) = delete;

    template <typename T> void register_component_array() {
        const auto idx = EntityComponentIdGenerator<>::get_id<T>();
        assert(idx < MAX_COMPONENTS);
        components.at(idx) = std::make_unique<ComponentArray<T>>();
    }
    template <typename T> T& get(Handle<Entity> handle) { return get_comp_array<T>()->data.at(Handle<T>{ *handle }); }
    template <typename T> uint64_t get_idx(Handle<Entity> handle) const {
        return get_comp_array<T>()->data.find_idx(Handle<T>{ *handle });
    }
    template <typename T> void insert(Handle<Entity> handle, T&& t) {
        using T_NOREF = std::remove_cvref_t<T>;
        get_comp_array<T_NOREF>()->data.insert(Handle<T_NOREF>{ *handle }, std::forward<T>(t));
    }
    template <typename T> std::span<const T> get_comps() const {
        ComponentArray<T>* arr = get_comp_array<T>();
        return std::span{ arr->data.begin(), arr->data.end() };
    }

  private:
    template <typename T> ComponentArray<T>* get_comp_array() {
        const auto idx = EntityComponentIdGenerator<>::get_id<T>();
        assert(components[idx]);
        return static_cast<ComponentArray<T>*>(components[idx].get());
    }

    std::array<std::unique_ptr<ComponentArrayBase>, MAX_COMPONENTS> components{};
};
