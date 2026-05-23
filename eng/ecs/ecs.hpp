#pragma once

#include <span>
#include <vector>
#include <array>
#include <stack>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <bitset>
#include <iterator>
#include <tuple>
#include <algorithm>
#include <limits>
#include <eng/common/sparseset.hpp>
#include <eng/common/slotmap.hpp>
#include <eng/common/slotallocator.hpp>
#include <eng/common/indexed_hierarchy.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/callback.hpp>
#include <eng/common/hash.hpp>

namespace eng
{
namespace ecs
{
/*
    Ecs is a system that generates handles (64 bit for now, but could be compressed)
    and allows users to attach structures to them which can be queried later.
    Each handle has 32bit versioning number which is used to prevent stale handles
    when it's been recycled when doing create->erase->create, and a slot which is a
    stable index that may be reused. During erase version is bumped by 1.

    EntityId::get_slot() returns stable index that may be used to index arrays with
    data associated with the entity.

    Typical usage is as follows:
    e = create()
    add_components(e, A{}, B{}, C{}, ...)
    has<A>(e)
    A[&] = get<A>(e)
    auto [A, B] = get<A, B>(e) - to get a tuple of references
    iterate_over_components<A, B>([](EntityId, [const] A[&], [const] B[&]) {});
    register_callbacks<A, B>({}, {}, [](EntityId) { cout << removed })
    erase(e)
    !has(e)

    Then some system may wish to iterate over all registered components of type A{}.
    Let's assume it's Transform component.
    It would do as follows
    iterate_over_components<A>([](EntityId, [const] A[&]) {});
    To iterate over entities that have at least components A and B:
    iterate_over_components<A, B>([](EntityId, [const] A[&], [const] B[&]) {});

    The syntax is the same for has<>(EntityId) and get<>(EntityId)

    Entities can also be in a relationship.
    You can make one entity a parent of another with make_child().
    There are also get_parent(), has_children(),
    traverse_hierarchy([](EntityId){}),
    loop_over_children([](EntityId){}).

    Traversing calls the callback with the starting entity, and traverses depth-first.
    Looping over just calls the callback with all it's children, in-order.
*/

namespace test
{
class EcsTest;
}

using EntitySlot = uint32_t;
using EntityVersion = uint32_t;
// typed uint64_t for storing stable index to associated data and a version
struct EntityId : public TypedId<EntityId, EntityVersion>
{
    explicit EntityId(StorageType handle) : TypedId(handle) {}
    explicit EntityId() : TypedId() {}
    EntityId(EntitySlot slot, EntityVersion version) : TypedId(((uint64_t)version << 32) | (uint64_t)slot) {}
    auto operator<=>(const EntityId&) const = default;
    EntitySlot slot() const { return EntitySlot{ (uint32_t)handle }; }
    uint32_t version() const { return (uint32_t)(handle >> 32); }
    EntityId bump() const { return EntityId{ slot(), version() + 1 }; }
};
using ComponentId = uint32_t;
inline static constexpr uint32_t MAX_COMPONENTS = std::numeric_limits<ComponentId>::digits;
using Signature = std::bitset<MAX_COMPONENTS>;

struct ComponentTraits
{
    template <typename Component> struct Id
    {
        static_assert(!std::is_reference_v<Component> && !std::is_pointer_v<Component>);
        static_assert(!std::is_const_v<Component> && !std::is_volatile_v<Component>);
        static_assert(std::is_object_v<Component>);
        static ComponentId id;
    };

    // Gets stable unique 0-based index for component
    template <typename Component> static ComponentId get_id() { return Id<std::remove_cvref_t<Component>>::id; }
    // Returns bit mask of given components
    template <typename... Components> static Signature get_signature()
    {
        return Signature{ (0 | ... | (1ull << get_id<Components>())) };
    }
};

struct IComponentPool
{
    virtual ~IComponentPool() = default;
    bool has(EntitySlot e) const { return entities.has(e); }
    size_t size() const { return entities.size(); }
    virtual void erase(EntitySlot e) = 0;
    SparseSet<EntitySlot> entities; // for packing components
};

template <typename Component> struct ComponentPool : public IComponentPool
{
    Component& get(EntitySlot e)
    {
        const auto idx = entities.to_dense(e);
        if(idx == entities.INVALID)
        {
            static Component null_component{};
            ENG_ASSERT(false, "Invalid entity {}", e);
            return null_component;
        }
        return components[idx];
    }

    template <typename... Args> void emplace(EntitySlot e, Args&&... args)
    {
        const bool has_e = entities.has(e);
        if(has_e)
        {
            ENG_WARN("Tried to overwrite entity {}", e);
            return;
        }
        const auto it = entities.allocate(e);
        ENG_ASSERT(it != entities.INVALID);
        if(it == entities.INVALID)
        {
            ENG_WARN("Failed to allocate storage for component for entity {}", it);
            return;
        }
        if(it < components.size()) { std::construct_at<Component>(&components[it], std::forward<Args>(args)...); }
        else
        {
            ENG_ASSERT(it == components.size());
            components.emplace_back(std::forward<Args>(args)...);
        }
    }

    void erase(EntitySlot e) override
    {
        const auto it = entities.free(e);
        if(it == entities.INVALID)
        {
            ENG_ERROR("Trying to delete invalid entity {}", e);
            return;
        }
        components[it] = std::move(components.back());
        components.pop_back();
    }

    std::vector<Component> components;
};

class Registry
{
    friend class eng::ecs::test::EcsTest;

    struct EntityMetadata
    {
        // Checks if entity has specified components
        bool has_components(Signature csig) const { return (csig & sig) == csig; }
        Signature sig{};
        EntityId parent{};
        EntityId next_sib{};
        EntityId prev_sib{};
        EntityId first_kid{};
    };

    template <typename... Components> static Signature get_signature()
    {
        return (ComponentTraits::get_signature<Components>() | ...);
    }

  public:
    // Checks if entity is registered. If stale entity handle was used, function will return false.
    // Stale handle has different version which changes after erasures.
    bool has(EntityId eid) const { return *eid < entities.size() && eid == entities[eid.slot()]; }

    // Checks if entity is registered and if it has specified components.
    template <typename... Components> bool has(EntityId eid) const
    {
        return has(eid) && get_md(eid).has_components(get_signature<Components...>());
    }

    // Creates an entity. Entity can be derefenced using * operator
    // And get_slot() returns stable index.
    EntityId create()
    {
        const auto hnid = entity_alloc.allocate();
        ENG_ASSERT(hnid, "Too many entities");
        if(!hnid) { return EntityId{}; }
        if(*hnid == metadatas.size()) { metadatas.emplace_back(); }
        if(*hnid == entities.size()) { entities.emplace_back(EntityId{ *hnid, 0u }); }
        ENG_ASSERT(*hnid == entities[*hnid].slot());
        return entities[*hnid];
    }

    // Removes associated components, removes entity and bumps up the version.
    void erase(EntityId eid)
    {
        if(!has(eid))
        {
            ENG_WARN("Tried to delete stale entity {}", *eid);
            return;
        }
        erase_components(eid);
        unparent_child(eid);
        entity_alloc.erase(eid.slot());
        entities[eid.slot()] = eid.bump();
    }

    // Returns reference to requested component. If entity is not
    // registered, returns reference to null component.
    template <typename Component> auto& get(this auto& self, EntityId eid)
    {
        if(!self.has(eid))
        {
            ENG_WARN("Invalid entity {}", *eid);
            static Component null_component[1]{};
            return std::forward_like<decltype(self)>(null_component[0]);
        }
        return std::forward_like<decltype(self)>(self.get_pool<Component>().get(eid.slot()));
    }

    // Attaches components to a valid entity. Returns if an entity already has one of the components.
    template <typename... Components> void add_components(EntityId eid, Components&&... components)
    {
        if(!has(eid))
        {
            ENG_WARN("Invalid entity {}", *eid);
            return;
        }

        const Signature sig = ComponentTraits::get_signature<Components...>();
        auto& md = get_md(eid);
        if((sig & md.sig).any())
        {
            ENG_WARN("Entity {} already has some of these components {}", *eid, (sig & md.sig).to_string());
            return;
        }

        const auto emplace_component = [this, eid](auto&& comp) {
            auto& pool = get_pool<std::decay_t<decltype(comp)>>();
            pool.emplace(eid.slot(), std::forward<decltype(comp)>(comp));
        };
        const auto old_sig = md.sig;
        md.sig |= sig;
        (emplace_component(std::forward<Components>(components)), ...);
    }

    // Return the count of registered entities.
    EntitySlot size() const { return entity_alloc.size(); }

    // Return the count of registered components of a given type.
    template <typename Component> EntitySlot size() const
    {
        if(auto* pool = try_get_pool<Component>()) { return pool->size(); }
        return 0;
    }

    // Tries to remove all present components from the entity.
    template <typename... Components> void erase_components(EntityId eid)
    {
        erase_components(eid, Signature{ ~ComponentId{} });
    }

    // Invokes a callback for every entity that has the specified components.
    template <typename... Components>
    void iterate_components(const auto& callback)
        requires(std::is_invocable_v<decltype(callback), EntityId, std::add_lvalue_reference_t<Components>...>)
    {
        const IComponentPool* const pool = try_find_smallest_pool<Components...>();
        const Signature sig = get_signature<Components...>();
        if(!pool) { return; }
        for(auto e : pool->entities)
        {
            const auto& md = metadatas[e];
            if(!md.has_components(sig)) { continue; }
            callback(entities[e], get<Components>(entities[e])...);
        }
    }

    // Creates parent-child relationship.
    void make_child(EntityId parentid, EntityId childid)
    {
        if(!has(parentid))
        {
            ENG_WARN("Parent entity {} is invalid", *parentid);
            return;
        }
        if(!has(childid))
        {
            ENG_WARN("Child entity {} is invalid", *childid);
            return;
        }
        auto* const pmd = &get_md(parentid);
        auto* const cmd = &get_md(childid);
        ENG_ASSERT(!cmd->parent);
        if(!pmd->first_kid) { pmd->first_kid = childid; }
        else
        {
            auto kid = pmd->first_kid;
            auto* kmd = &get_md(pmd->first_kid);
            while(kmd->next_sib)
            {
                kid = kmd->next_sib;
                kmd = &get_md(kmd->next_sib);
            }
            kmd->next_sib = childid;
            cmd->prev_sib = kid;
        }
        cmd->parent = parentid;
    }

    void unparent_child(EntityId childid)
    {
        if(!has(childid))
        {
            ENG_WARN("Child entity {} is invalid", *childid);
            return;
        }
        auto* const cmd = &get_md(childid);
        if(!cmd->parent) { return; }
        auto* const pmd = &get_md(cmd->parent);
        if(pmd->first_kid == childid) { pmd->first_kid = cmd->next_sib; }
        else { get_md(cmd->prev_sib).next_sib = cmd->next_sib; }
        if(cmd->next_sib) { get_md(cmd->next_sib).prev_sib = cmd->prev_sib; }
        cmd->parent = EntityId{};
        cmd->next_sib = EntityId{};
        cmd->prev_sib = EntityId{};
    }

    // Gets the parent of an entity.
    EntityId get_parent(EntityId eid) const
    {
        if(!has(eid))
        {
            ENG_WARN("Entity {} is invalid", *eid);
            return EntityId{};
        }
        return get_md(eid).parent;
    }

    // Returns true if an entity is a parent to other entity
    bool has_children(EntityId eid) const
    {
        if(!has(eid))
        {
            ENG_WARN("Entity {} is invalid", *eid);
            return false;
        }
        return get_md(eid).first_kid != EntityId{};
    }

    void iterate_entities(const auto& callback)
        requires(std::is_invocable_v<decltype(callback), EntityId>)
    {
        for(auto e : entities)
        {
            std::invoke(callback, e);
        }
    }

    // Invokes a callback for every child of this entity.
    void iterate_children(EntityId eid, const auto& callback)
    {
        if(!has(eid))
        {
            ENG_WARN("Entity {} is invalid", *eid);
            return;
        }
        for(auto it = get_md(eid).first_kid; it; it = get_md(it).next_sib)
        {
            callback(it);
        }
    }

    // Traverses depth-first the relationship hierarchy of given entity.
    // Callback is called at least once for any valid entity.
    void traverse_hierarchy(EntityId eid, const auto& callback)
    {
        if(!has(eid))
        {
            ENG_ERROR("Invalid entity {}", *eid);
            return;
        }
        const auto traverse = [&](EntityId id, const auto& self) -> void {
            callback(id);
            iterate_children(id, [&](EntityId id) { self(id, self); });
        };
        traverse(eid, traverse);
    }

  private:
    auto& get_md(this auto& self, EntityId eid) { return self.metadatas[eid.slot()]; }

    void erase_components(EntityId eid, Signature sig)
    {
        if(!has(eid))
        {
            ENG_WARN("Invalid entity {}", *eid);
            return;
        }
        auto& md = get_md(eid);
        if((md.sig & sig).none()) { return; }
        for(auto i = 0ull; i < sig.size(); ++i)
        {
            if(sig[i] && md.sig[i]) { pools[i]->erase(eid.slot()); }
        }
        const auto newsig = md.sig & (~sig);
        md.sig = newsig;
    }

    template <typename Component> ComponentPool<Component>& get_pool()
    {
        const auto id = ComponentTraits::get_id<Component>();
        if(!pools[id]) { pools[id] = std::make_unique<ComponentPool<Component>>(); }
        return *static_cast<ComponentPool<Component>*>(&*pools[id]);
    }

    template <typename Component> ComponentPool<Component>* try_get_pool() const
    {
        const auto id = ComponentTraits::get_id<Component>();
        if(!pools[id]) { return nullptr; }
        return *static_cast<ComponentPool<Component>*>(&*pools[id]);
    }

    template <typename... Components> IComponentPool* try_find_smallest_pool()
    {
        return try_find_smallest_pool(get_signature<Components...>());
    }

    IComponentPool* try_find_smallest_pool(Signature sig)
    {
        if(sig.none()) { return nullptr; }
        IComponentPool* smallest{};
        for(auto i = 0ull; i < sig.size(); ++i)
        {
            if(sig[i] && (!smallest || smallest->size())) { smallest = &*pools[i]; }
        }
        return smallest;
    }

    SlotAllocator<EntitySlot> entity_alloc;
    std::vector<EntityId> entities;        // besides indices from hierarchy, stores versions for erase() and has()
    std::vector<EntityMetadata> metadatas; // additional info for entities
    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENTS> pools; // component pools
};

} // namespace ecs
} // namespace eng

ENG_DEFINE_STD_HASH(eng::ecs::EntityId, *t);
