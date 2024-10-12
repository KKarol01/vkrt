#pragma once
#include <atomic>
#include <array>
#include <cassert>
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
    inline static constexpr u32 MAX_COMPONENTS = 16;
    constexpr EntityComponents() = default;
    ~EntityComponents() {
        for(u32 i = 0; i < MAX_COMPONENTS; ++i) {
            delete components[i];
        }
    }
    EntityComponents(const EntityComponents&) = delete;
    EntityComponents& operator=(const EntityComponents&) = delete;
    EntityComponents(EntityComponents&&) = delete;
    EntityComponents& operator=(EntityComponents&&) = delete;

    template <typename T> void register_component_array() {
        const auto idx = EntityComponentIdGenerator<>::get_id<T>();
        assert(idx < MAX_COMPONENTS);
        components.at(idx) = new ComponentArray<T>{};
    }
    template <typename T> T& get(Handle<Entity> handle) { return get_comp_array<T>()->data.at(Handle<T>{ *handle }); }
    template <typename T> void insert(Handle<Entity> handle, T&& t) {
        return Handle<Entity>{ *get_comp_array<T>()->data.insert(handle, std::forward<T>(t)) };
    }

  private:
    template <typename T> ComponentArray<T>* get_comp_array() {
        const auto idx = EntityComponentIdGenerator<>::get_id<T>();
        assert(components[idx]);
        return static_cast<ComponentArray<T>*>(components[idx]);
    }

    std::array<ComponentArrayBase*, MAX_COMPONENTS> components{};
};
