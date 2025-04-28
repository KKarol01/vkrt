#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <cassert>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <memory>
#include <eng/common/sparseset.hpp>

namespace components {
using Entity = uint32_t;
using component_id_t = uint32_t;
inline static constexpr Entity s_max_entity = ~Entity{};
inline static constexpr Entity s_max_components = 32;

class ComponentArrayBase {
  public:
    virtual ~ComponentArrayBase() = default;
    virtual void destroy(Entity e) = 0;
};

template <typename T> class ComponentArray : public ComponentArrayBase {
  public:
    T* try_get(Entity e) {
        if(!entities.has(e)) { return nullptr; }
        return &components.at(entities.get(e).dense_idx);
    }

    template <typename... Args>
    T& emplace(Entity e, Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        if(entities.has(e)) { return *try_get(e); }
        entities.insert(e);
        return components.emplace_back(std::forward<Args>(args)...);
    }

    void destroy(Entity e) final {
        if(!entities.has(e)) { return; }
        const auto it = entities.get(e);
        std::swap(components.at(it.dense_idx), components.at(components.size() - 1));
        components.erase(components.end() - 1);
        entities.destroy(e);
    }

    bool has(Entity e) const { return entities.has(e); }

    size_t size() const { return entities.size(); }

  private:
    SparseSet<> entities;
    std::vector<T> components;
};

struct ComponentIdGenerator {
    template <typename Component> static component_id_t generate() {
        static component_id_t id = _id++;
        return id;
    }
    inline static std::atomic<component_id_t> _id{ component_id_t{} };
};

class Storage {
  public:
    Entity create() { return *entities.insert().key; }

    template <typename Component, typename... Args> Component& emplace(Entity e, Args&&... args) {
        return *try_emplace<Component>(e, std::forward<Args>(args)...);
    }

    void destroy(Entity e) {
        for(auto& arr : component_arrays) {
            if(arr) { arr->destroy(e); }
        }
        entities.destroy(e);
    }

    template <typename Component> Component& get(Entity e) { return *get_or_make_comp_arr<Component>().try_get(e); }
    template <typename Component> Component* try_get(Entity e) { return get_or_make_comp_arr<Component>().try_get(e); }

    template <typename... Components> void for_each(auto f) {
        std::array<size_t, sizeof...(Components)> comp_ids;
        size_t i = 0, min_size = ~0ull;
        (..., ([this, &i, &min_size, &comp_ids]<typename T>() {
                  min_size = std::min(min_size, get_or_make_comp_arr<T>().size());
                  comp_ids.at(i++) = ComponentIdGenerator::generate<T>();
              }).template operator()<Components>());
        auto has_comp = [this]<typename T>(Entity e) { return get_or_make_comp_arr<T>().has(e); };
        for(auto e : entities.get_dense()) {
            if(min_size == 0) { break; }
            if((has_comp.template operator()<Components>(e) && ...)) {
                f((*get_or_make_comp_arr<Components>().try_get(e))...);
                --min_size;
            }
        }
    }

    template <typename Component> bool has_component(Entity e) const {
        const auto idx = ComponentIdGenerator::generate<Component>();
        if(!component_arrays.at(idx)) { return false; }
        return static_cast<ComponentArray<Component>*>(&*component_arrays.at(idx))->has(e);
    }

  private:
    template <typename Component, typename... Args> Component* try_emplace(Entity e, Args&&... args) {
        if(!entities.has(e)) { return nullptr; }
        auto& comp_arr = get_or_make_comp_arr<Component>();
        return &comp_arr.emplace(e, std::forward<Args>(args)...);
    }

    template <typename Component> auto& get_or_make_comp_arr() {
        auto& comp_arr = component_arrays.at(ComponentIdGenerator::generate<Component>());
        if(!comp_arr) { comp_arr = std::make_unique<ComponentArray<Component>>(); }
        return *static_cast<ComponentArray<Component>*>(&*comp_arr);
    }

    std::array<std::unique_ptr<ComponentArrayBase>, s_max_components> component_arrays;
    SparseSet<uint32_t> entities;
};
} // namespace components