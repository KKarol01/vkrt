#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <cassert>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <memory>

namespace components {
using Entity = uint32_t;
using component_id_t = uint32_t;
inline static constexpr Entity MAX_ENTITY = ~Entity{};
inline static constexpr Entity MAX_COMPONENTS = 32;

class SparseSet {
  public:
    struct Iterator {
        const Entity* entity{};
        size_t dense_idx{};
    };

    Iterator insert(Entity e) {
        if(e == MAX_ENTITY) { return Iterator{}; }
        if(has(e)) { return make_iterator(e); }
        maybe_resize_sparse(e);
        sparse.at(e) = free_list_head;
        dense.insert(dense.begin() + free_list_head, e);
        ++free_list_head;
        return make_iterator(e);
    }

    Iterator insert() {
        Iterator it{};
        size_t cur = free_list_head;
        while(cur < dense.size()) {
            const auto val = dense.at(cur);
            if(has(val)) {
                ++cur;
                continue;
            }
            dense.erase(dense.begin() + free_list_head, dense.begin() + cur);
            assert(dense.at(free_list_head) == val);
            sparse.at(val) = free_list_head;
            ++free_list_head;
            return make_iterator(val);
        }
        if(cur != free_list_head) { dense.erase(dense.begin() + free_list_head, dense.end()); }
        return insert(free_list_head);
    }

    bool has(Entity e) const {
        return e < MAX_ENTITY && sparse.size() > e && free_list_head > sparse.at(e) && dense.at(sparse.at(e)) == e;
    }

    void destroy(Entity e) {
        if(!has(e)) { return; }
        const auto idx = sparse.at(e);
        std::swap(dense.at(idx), dense.at(--free_list_head));
        sparse.at(dense.at(idx)) = idx;
    }

    Iterator get(Entity e) const { return make_iterator(e); }

    size_t size() const { return dense.size(); }

    std::span<const Entity> get_dense() const { return std::span{ dense.begin(), dense.end() }; }

  private:
    Iterator make_iterator(Entity e) const {
        const auto* ptr = &dense.at(sparse.at(e));
        return { ptr, static_cast<size_t>(ptr - dense.data()) };
    }
    void maybe_resize_sparse(Entity e) {
        if(sparse.size() <= e) { sparse.resize(e + 1); }
    }

    std::vector<Entity> sparse;
    std::vector<Entity> dense;
    size_t free_list_head{};
};

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
    SparseSet entities;
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
    Entity create() { return *entities.insert().entity; }

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

    std::array<std::unique_ptr<ComponentArrayBase>, MAX_COMPONENTS> component_arrays;
    SparseSet entities;
};
} // namespace components