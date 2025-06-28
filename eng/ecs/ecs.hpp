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
#include <bitset>
#include <algorithm>
#include <unordered_map>
#include <eng/common/sparseset.hpp>
#include <eng/common/logger.hpp>

namespace ecs
{
using Entity = uint32_t;
using component_id_t = uint32_t;
inline static constexpr Entity INVALID_ENTITY = ~Entity{};
inline static constexpr Entity MAX_COMPONENTS = sizeof(component_id_t) * 8;

struct ComponentIdGenerator
{
    template <typename Component> static component_id_t generate()
    {
        assert(_id.load() < MAX_COMPONENTS);
        static component_id_t id = component_id_t{ 1 } << (_id++);
        return id;
    }
    inline static std::atomic<component_id_t> _id{};
};

class IComponentPool
{
  public:
    virtual ~IComponentPool() = default;
    virtual void* get(Entity e) = 0;
    virtual void erase(Entity e) = 0;
};

template <typename T> class ComponentPool : public IComponentPool
{
  public:
    void* get(Entity e) final
    {
        const auto it = entities.get(e);
        if(!it)
        {
            ENG_ERROR("Invalid entity {} for component {}", e, ComponentIdGenerator::generate<T>());
            return nullptr;
        }
        return &components.at(it.index);
    }

    template <typename... Args>
    T& emplace(Entity e, Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        const auto it = entities.insert(e);
        if(!it)
        {
            ENG_WARN("Overwriting already existing component for entity {}. Skipping.", e);
            assert(false);
            return *static_cast<T*>(get(it.index));
        }
        if(it.index < components.size()) { components.at(it.index) = T{ std::forward<Args>(args)... }; }
        else if(it.index == components.size()) { components.emplace_back(std::forward<Args>(args)...); }
        else { assert(false); }
        return *static_cast<T*>(get(it.index));
    }

    void erase(Entity e) final
    {
        const auto it = entities.erase(e);
        if(!it) { return; }
        components.at(it.index) = std::move(components.at(entities.size()));
    }

    size_t size() const { return entities.size(); }

  private:
    SparseSet entities;
    std::vector<T> components;
};

class Registry
{
    struct EntityMetadata
    {
        std::bitset<MAX_COMPONENTS> components;
        Entity parent{ INVALID_ENTITY };
        std::vector<Entity> children;
    };

  public:
    Entity create()
    {
        const auto e = entities.get(entities.insert());
        if(e == INVALID_ENTITY)
        {
            ENG_WARN("Max entity reached");
            assert(false);
            return e;
        }
        metadatas.emplace(e, EntityMetadata{});
        return e;
    }

    bool has(Entity e) const { return is_valid(e); }

    template <typename Component> bool has(Entity e) const
    {
        if(is_valid(e))
        {
            const auto idx = ComponentIdGenerator::generate<Component>();
            return metadatas.at(e).components.test(idx);
        }
        return false;
    }

    template <typename Component> Component* get(Entity e)
    {
        if(!is_valid(e) || !has<Component>(e)) { return nullptr; }
        const auto idx = ComponentIdGenerator::generate<Component>();
        return static_cast<Component*>(component_arrays.at(idx)->get(e));
    }

    template <typename Component> const Component* get(Entity e) const { return const_cast<Registry*>(this)->get(e); }

    template <typename Component, typename... Args> Component* emplace(Entity e, Args&&... args)
    {
        if(!is_valid(e)) { return nullptr; }
        const auto cidx = ComponentIdGenerator::generate<Component>();
        metadatas.at(e).components.set(cidx);
        auto& comp_arr = get_comp_arr<Component>();
        return &comp_arr.emplace(e, std::forward<Args>(args)...);
    }

    void erase(Entity e)
    {
        if(!is_valid(e)) { return; }
        auto& md = metadatas.at(e);
        for(auto i = 0u; i < MAX_COMPONENTS; ++i)
        {
            if(md.components.test(i)) { component_arrays.at(i)->erase(e); }
        }
        if(md.parent != INVALID_ENTITY) { remove_child(md.parent, e); }
        for(auto c : md.children)
        {
            erase(c);
        }
        metadatas.erase(e);
        entities.erase(e);
    }

    template <typename Component> void erase(Entity e)
    {
        if(!is_valid(e)) { return; }
        auto& md = metadatas.at(e);
        const auto idx = ComponentIdGenerator::generate<Component>();
        if(md.components.test(idx))
        {
            component_arrays.at(idx)->erase(e);
            md.components.reset(idx);
        }
    }

    void make_child(Entity p, Entity c)
    {
        if(is_valid(p) && is_valid(c))
        {
            auto& pmd = metadatas.at(p);
            auto& cmd = metadatas.at(c);
            if(cmd.parent == p) { return; }
            if(cmd.parent != INVALID_ENTITY) { remove_child(cmd.parent, c); }
            cmd.parent = p;
            auto& ch = pmd.children;
            const auto it = std::lower_bound(ch.begin(), ch.end(), c);
            if(it != ch.end() && *it == c)
            {
                ENG_ERROR("Entity {} is already a child of Entity {}", c, p);
                return;
            }
            ch.insert(it, c);
        }
    }

    void remove_child(Entity p, Entity c)
    {
        if(is_valid(p) && is_valid(c))
        {
            metadatas.at(c).parent = INVALID_ENTITY;
            auto& ch = metadatas.at(p).children;
            const auto it = std::lower_bound(ch.begin(), ch.end(), c);
            if(it != ch.end() && *it == c) { ch.erase(it); }
        }
    }

    void traverse_hierarchy(Entity e, const auto& callback)
    {
        const auto dfs = [this, callback](Entity e, const auto& recursive) {
            if(!is_valid(e)) { return; }
            callback(e);
            auto& ch = metadatas.at(e).children;
            for(auto c : ch)
            {
                recursive(c, recursive);
            }
        };
        dfs(e, dfs);
    }

  private:
    bool is_valid(Entity e) const
    {
        const auto res = e != INVALID_ENTITY && has(e);
        if(!res) { ENG_ERROR("Entity {} is invalid.", e); }
        return res;
    }

    template <typename Component> auto& get_comp_arr()
    {
        auto& comparr = component_arrays.at(ComponentIdGenerator::generate<Component>());
        if(!comparr) { comparr = std::make_unique<ComponentPool<Component>>(); }
        return *static_cast<ComponentPool<Component>*>(&*comparr);
    }

    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENTS> component_arrays;
    std::unordered_map<Entity, EntityMetadata> metadatas;
    SparseSet entities;
};
} // namespace ecs
