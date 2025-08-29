#pragma once

#include <span>
#include <vector>
#include <array>
#include <stack>
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

namespace eng
{
namespace ecs
{
using entity = uint32_t;
using component_id_t = uint32_t;
inline static constexpr entity INVALID_ENTITY = ~entity{};
inline static constexpr entity MAX_COMPONENTS = sizeof(component_id_t) * 8;

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
    virtual void* get(entity e) = 0;
    virtual void erase(entity e) = 0;
    virtual void clone(entity src, entity dst) = 0;
};

template <typename T> class ComponentPool : public IComponentPool
{
  public:
    auto begin() { return components.begin(); }
    auto end() { return components.begin() + size(); }

    void* get(entity e) final
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
    T& emplace(entity e, Args&&... args)
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
        return *static_cast<T*>(get(e));
    }

    void erase(entity e) final
    {
        const auto it = entities.erase(e);
        if(!it) { return; }
        components.at(it.index) = std::move(components.at(entities.size()));
    }

    void clone(entity src, entity dst) final
    {
        const auto srcit = entities.get(src);
        auto dstit = entities.get(dst);
        if(!srcit)
        {
            ENG_ERROR("Invalid entity for component cloning: {}, {}", src, dst);
            return;
        }
        if(!dstit) { emplace(dst, components.at(srcit.index)); }
        else { components.at(dstit.index) = components.at(srcit.index); }
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
        entity parent{ INVALID_ENTITY };
        std::vector<entity> children;
    };

  public:
    template <typename Component> std::span<const Component> get_components()
    {
        const auto& arr = get_comp_arr<Component>();
        return std::span{ arr.begin(), arr.end() };
    }

    entity create()
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

    bool has(entity e) const { return entities.has(e); }

    template <typename Component> bool has(entity e) const
    {
        if(is_valid(e))
        {
            const auto idx = ComponentIdGenerator::generate<Component>();
            return metadatas.at(e).components.test(idx);
        }
        return false;
    }

    template <typename Component> Component* get(entity e)
    {
        if(!is_valid(e) || !has<Component>(e)) { return nullptr; }
        const auto idx = ComponentIdGenerator::generate<Component>();
        return static_cast<Component*>(component_arrays.at(idx)->get(e));
    }

    template <typename Component> const Component* get(entity e) const { return const_cast<Registry*>(this)->get(e); }

    template <typename Component, typename... Args> Component* emplace(entity e, Args&&... args)
    {
        if(!is_valid(e)) { return nullptr; }
        const auto cidx = ComponentIdGenerator::generate<Component>();
        metadatas.at(e).components.set(cidx);
        auto& comp_arr = get_comp_arr<Component>();
        return &comp_arr.emplace(e, std::forward<Args>(args)...);
    }

    void erase(entity e)
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

    template <typename Component> void erase(entity e)
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

    entity get_parent(entity e) const
    {
        if(!is_valid(e)) { return INVALID_ENTITY; }
        return metadatas.at(e).parent;
    }

    const std::vector<entity>& get_children(entity e) const
    {
        if(is_valid(e)) { return metadatas.at(e).children; }
        return {};
    }

    void make_child(entity p, entity c)
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

    void remove_child(entity p, entity c)
    {
        if(is_valid(p) && is_valid(c))
        {
            metadatas.at(c).parent = INVALID_ENTITY;
            auto& ch = metadatas.at(p).children;
            const auto it = std::lower_bound(ch.begin(), ch.end(), c);
            if(it != ch.end() && *it == c) { ch.erase(it); }
        }
    }

    void traverse_hierarchy(entity e, const auto& callback)
    {
        if(!is_valid(e)) { return; }
        const auto dfs = [this, callback](entity p, entity e, const auto& self) -> void {
            callback(p, e);
            auto& ch = metadatas.at(e).children;
            for(auto c : ch)
            {
                self(e, c, self);
            }
        };
        dfs(INVALID_ENTITY, e, dfs);
    }

    entity clone(entity src)
    {
        if(!is_valid(src)) { return INVALID_ENTITY; }
        std::stack<entity> cparents;
        entity ret = INVALID_ENTITY;
        traverse_hierarchy(src, [this, &cparents, &ret](auto p, auto e) {
            auto clone = create();
            if(ret == INVALID_ENTITY) { ret = clone; }
            if(cparents.size())
            {
                auto pclone = cparents.top();
                cparents.pop();
                make_child(pclone, clone);
            }
            EntityMetadata& emd = metadatas.at(e);
            for(auto i = 0u; i < MAX_COMPONENTS; ++i)
            {
                if(emd.components.test(i))
                {
                    metadatas.at(clone).components.set(i, true);
                    component_arrays.at(i)->clone(e, clone);
                }
            }
            for(auto i = 0u; i < emd.children.size(); ++i)
            {
                cparents.push(clone);
            }
        });
        assert(cparents.empty());
        return ret;
    }

  private:
    bool is_valid(entity e) const
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
    std::unordered_map<entity, EntityMetadata> metadatas;
    SparseSet entities;
};
} // namespace ecs
} // namespace eng
