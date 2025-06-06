#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <cassert>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <eng/common/sparseset.hpp>
#include <eng/common/logger.hpp>

namespace ecs {
using Entity = uint32_t;
using component_id_t = uint32_t;
inline static constexpr Entity s_max_entity = ~Entity{};
inline static constexpr Entity s_max_components = 32;

class IComponentPool {
  public:
    virtual ~IComponentPool() = default;
    virtual void erase(Entity e) = 0;
};

template <typename T> class ComponentPool : public IComponentPool {
  public:
    T& get(Entity e) { return *try_get(e); }

    T* try_get(Entity e) {
        const auto it = entities.get(e);
        if(!it) { return nullptr; }
        return &components.at(it.dense_idx);
    }

    template <typename... Args>
    T& emplace(Entity e, Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        const auto it = entities.insert(e);
        if(!it) {
            ENG_WARN("Overwriting already existing component for entity {}.", e);
            auto* c = new (&get(e)) T{ std::forward<Args>(args)... };
            return *c;
        }
        maybe_resize(it.dense_idx);
        if(components.size() <= it.dense_idx) {
            assert(components.size() == it.dense_idx);
            components.emplace_back(std::forward<Args>(args)...);
        } else {
            new (components.data() + it.dense_idx) T{ std::forward<Args>(args)... };
        }
        return components.at(it.dense_idx);
    }

    void erase(Entity e) final {
        const auto it = entities.get(e);
        if(!it) { return; }
        entities.erase(e);
        components.at(it.dense_idx) = std::move(components.at(entities.size()));
    }

    bool has(Entity e) const { return entities.has(e); }

    size_t size() const { return entities.size(); }

  private:
    void maybe_resize(size_t idx) {
        if(components.capacity() != entities.get_dense_capacity()) {
            components.reserve(entities.get_dense_capacity());
        }
        assert(components.capacity() > idx && idx <= components.size());
    }

    SparseSet<Entity> entities;
    std::vector<T> components;
};

struct ComponentIdGenerator {
    template <typename Component> static component_id_t generate() {
        static component_id_t id = _id++;
        return id;
    }
    inline static std::atomic<component_id_t> _id{ component_id_t{} };
};

class Registry {
  public:
    Entity create() { return entities.get_dense(entities.insert()); }

    template <typename Component, typename... Args> Component* emplace(Entity e, Args&&... args) {
        if(!entities.has(e)) {
            ENG_ERROR("Entity {} does not exist.", e);
            assert(false);
            return nullptr;
        }
        auto& comp_arr = get_or_make_comp_arr<Component>();
        return &comp_arr.emplace(e, std::forward<Args>(args)...);
    }

    void erase(Entity e) {
        for(auto& arr : component_arrays) {
            if(arr) { arr->erase(e); }
        }
        entities.erase(e);
    }

    template <typename Component> Component& get(Entity e) { return *get_or_make_comp_arr<Component>().try_get(e); }
    template <typename Component> Component* try_get(Entity e) { return get_or_make_comp_arr<Component>().try_get(e); }

    bool has(Entity e) const { return entities.has(e); }
    template <typename Component> bool has(Entity e) const {
        if(!has(e)) { return false; }
        const auto idx = ComponentIdGenerator::generate<Component>();
        if(!component_arrays.at(idx)) { return false; }
        return static_cast<ComponentPool<Component>*>(&*component_arrays.at(idx))->has(e);
    }

  private:
    template <typename Component> auto& get_or_make_comp_arr() {
        auto& comp_arr = component_arrays.at(ComponentIdGenerator::generate<Component>());
        if(!comp_arr) { comp_arr = std::make_unique<ComponentPool<Component>>(); }
        return *static_cast<ComponentPool<Component>*>(&*comp_arr);
    }

    std::array<std::unique_ptr<IComponentPool>, s_max_components> component_arrays;
    SparseSet<Entity> entities;
};
} // namespace ecs