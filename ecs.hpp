#pragma once
#include <atomic>
#include <array>
#include <cassert>
#include <span>
#include <memory>
#include "handle.hpp"
#include "common/types.hpp"
#include "handle_vec.hpp"

template <typename Storage = u32> struct EntityComponentIdGenerator {
    template <typename T> static Storage get_id() {
        static Storage idx = counter++;
        return idx;
    };
    inline static std::atomic<Storage> counter{ Storage{} };
};

struct ComponentArrayBase {
    virtual ~ComponentArrayBase() = default;
};

template <typename T, typename Storage = u32> struct ComponentArray : public ComponentArrayBase {
    HandleVector<T, Storage> data;
};

class EntityComponents {
  public:
    inline static constexpr u32 MAX_COMPONENTS = 32; // components' ids get stored in u32 bitfield to indicate presence
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
    template <typename T> u64 get_idx(Handle<Entity> handle) const {
        return get_comp_array<T>()->data.find_idx(Handle<T>{ *handle });
    }
    template <typename T> void insert(Handle<Entity> handle, T&& t) {
        get_comp_array<T>()->data.insert(Handle<T>{ *handle }, std::forward<T>(t));
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
