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

    The system also allows for callbacks that can be notfied
    when a new component gets added, removed or updated.
    register_callbacks<Components...>(on_insert, on_update, on_remove) takes optional callbacks
    that: get called when there is a new entity that has required components --
        for example if you care about entities that have a mesh and a position so you
        can render them, this callback would get called once for this entity when
        it gains at least those components;

        get called when somebody modified the components and wishes to notify
        anyone who cares about a particular component, for example when you
        register a callback that listens on any entity that has at least
        Transform, Mesh and Collision components, and somebody modified transform;

        get called when somebody removed entity or a component and it no longer satisfies your
        component requirements -- if someone have removed Collision component, callbacks
        for <Transform, Mesh, Collision> would be notified with on_remove, but callbacks
        for <Transform, Mesh> would not, because the entity still has those.

    To notify about the change a component, call signal_components_update<UpdatedComponents...>(eid).
*/

namespace test
{
class EcsTest;
}

struct Slot;
using SlotId = IndexedHierarchy::NodeId; // indexed to linear arrays of entities and other associated data
// typed uint64_t for storing stable index to associated data and a version
struct EntityId : public TypedId<EntityId, uint64_t>
{
    explicit EntityId(storage_type handle) : TypedId(handle) {}
    explicit EntityId() : TypedId() {}
    EntityId(SlotId slot, uint32_t version) : TypedId(((uint64_t)version << 32) | (uint64_t)*slot) {}
    auto operator<=>(const EntityId&) const = default;
    SlotId get_slot() const { return SlotId{ (uint32_t)handle }; }
    uint32_t get_version() const { return (uint32_t)(handle >> 32); }
};
using ComponentId = uint32_t;
inline static constexpr uint32_t MAX_COMPONENTS = std::numeric_limits<ComponentId>::digits;
using Signature = std::bitset<MAX_COMPONENTS>;
using ViewEntityInsertedFunc = void(EntityId eid);
using ViewEntityUpdatedFunc = void(EntityId eid, Signature updated);
using ViewEntityRemovedFunc = void(EntityId eid);

struct ComponentTraits
{
    template <typename Component> static ComponentId get_id_()
    {
        static_assert(!std::is_reference_v<Component> && !std::is_pointer_v<Component>);
        static_assert(!std::is_const_v<Component> && !std::is_volatile_v<Component>);
        static_assert(std::is_object_v<Component>);
        static auto id = counter.fetch_add(1);
        ENG_ASSERT(id < MAX_COMPONENTS);
        return id;
    }

    // Gets stable unique 0-based index for component
    template <typename Component> static ComponentId get_id() { return get_id_<std::remove_cvref_t<Component>>(); }
    // Returns bit mask of given components
    template <typename... Components> static Signature get_signature()
    {
        return Signature{ (0 | ... | (1ull << get_id<Components>())) };
    }
    inline static std::atomic<ComponentId> counter{};
};

template <typename... Components> inline bool test_signature(Signature sig)
{
    const auto csig = ComponentTraits::get_signature<Components...>();
    return (csig & sig) == csig;
}

struct IComponentPool
{
    virtual ~IComponentPool() = default;
    bool has(SlotId e) const { return entities.has(*e); }
    size_t size() const { return entities.size(); }
    virtual void erase(SlotId e) = 0;
    SparseSet<SlotId::storage_type, 1024> entities; // for packing components
};

template <typename Component> struct ComponentPool : public IComponentPool
{
    Component& get(SlotId e)
    {
        const auto idx = entities.get(*e);
        if(!idx) { ENG_ERROR("Invalid entity {}", *e); }
        return components[*idx];
    }

    template <typename... Args> void emplace(SlotId e, Args&&... args)
    {
        const auto it = entities.insert(*e);
        if(!it)
        {
            ENG_ERROR("Overwriting entity {}", *e);
            return;
        }
        if(*it < components.size()) { std::construct_at<Component>(&components[*it], std::forward<Args>(args)...); }
        else
        {
            ENG_ASSERT(*it == components.size());
            components.emplace_back(std::forward<Args>(args)...);
        }
    }

    void erase(SlotId e) override
    {
        const auto it = entities.erase(*e);
        if(!it)
        {
            ENG_ERROR("Trying to delete invalid entity {}", *e);
            return;
        }
        components[*it] = std::move(components.back());
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
        Signature sig;
    };

    // Collects all entities with given signature for fast iteration
    struct View
    {
        bool accepts_signature(Signature esig) const { return (sig & esig) == sig; }
        Signature sig;
        std::vector<EntityId> entities;
        Signal<ViewEntityInsertedFunc> on_insert_callbacks;
        Signal<ViewEntityUpdatedFunc> on_update_callbacks;
        Signal<ViewEntityRemovedFunc> on_remove_callbacks;
    };

    template <typename... Components> static Signature get_signature()
    {
        return (ComponentTraits::get_signature<Components>() | ...);
    }

  public:
    // Checks if entity is registered. Can fail if versions mismatch on previous erase.
    bool has(EntityId eid) const { return *eid < entities.size() && eid == entities[*eid.get_slot()]; }

    // Checks if entity is registered and if it has components.
    template <typename... Components> bool has(EntityId eid) const
    {
        return has(eid) && get_md(eid).has_components(get_signature<Components...>());
    }

    // Creates an entity. Entity can be derefenced using * operator
    // And get_slot() returns stable index.
    EntityId create()
    {
        const auto hnid = hierarchy.create();
        if(!hnid)
        {
            ENG_ASSERT(false, "Too many entities");
            return EntityId{};
        }
        if(*hnid == metadatas.size()) { metadatas.emplace_back(); }
        if(*hnid == entities.size()) { entities.emplace_back(EntityId{ hnid, 0 }); }
        ENG_ASSERT(hnid == entities[*hnid].get_slot());
        return entities[*hnid];
    }

    // Removes associated components, removes entity and bumps up the version.
    void erase(EntityId eid)
    {
        if(!has(eid))
        {
            ENG_ERROR("Tried to delete stale entity {}", *eid);
            return;
        }
        erase_components(eid);
        hierarchy.erase(eid.get_slot());
        entities[*eid.get_slot()] = EntityId{ eid.get_slot(), eid.get_version() + 1 };
    }

    // Returns a tuple of references to queried components or a single reference.
    // If entity does not exist, returns references to null_objects to avoid hard errors.
    template <typename... Components> decltype(auto) get(EntityId eid)
    {
        static_assert(sizeof...(Components) > 0);
        if constexpr(sizeof...(Components) == 1)
        {
            using T = std::tuple_element_t<0, std::tuple<Components...>>;
            if(!has(eid))
            {
                ENG_ERROR("Invalid entity {}", *eid);
                static T null_component{};
                return (null_component);
            }
            return (get_pool<T>().get(eid.get_slot()));
        }
        else { return std::tuple<Components&...>{ get<Components>(eid)... }; }
    }

    // Attaches components to a valid entity. Returns if an entity already has one of the components.
    template <typename... Components> void add_components(EntityId eid, Components&&... components)
    {
        if(!has(eid))
        {
            ENG_ERROR("Invalid entity {}", *eid);
            return;
        }

        const Signature sig = ComponentTraits::get_signature<Components...>();
        auto& md = get_md(eid);
        if((sig & md.sig).any())
        {
            ENG_ERROR("Entity {} already has some of these components {}", *eid, (sig & md.sig).to_string());
            return;
        }

        const auto emplace_component = [this, eid]<typename T>(T&& comp) {
            auto& pool = get_pool<std::decay_t<T>>();
            pool.emplace(eid.get_slot(), std::forward<decltype(comp)>(comp));
        };
        const auto old_sig = md.sig;
        md.sig |= sig;
        (emplace_component.template operator()<Components>(std::forward<Components>(components)), ...);
        on_entity_sig_change(eid, old_sig, md.sig);
    }

    // Tries to remove all present components from the entity.
    template <typename... Components> void erase_components(EntityId eid)
    {
        erase_components(eid, Signature{ ~ComponentId{} });
    }

    // Invokes a callback for every entity that has the specified components.
    template <typename... Components>
    void iterate_over_components(const auto& callback)
        requires(std::is_invocable_v<decltype(callback), EntityId, Components...>)
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
            ENG_ERROR("Entity {} is invalid", *parentid);
            return;
        }
        if(!has(childid))
        {
            ENG_ERROR("Entity {} is invalid", *childid);
            return;
        }
        hierarchy.make_child(parentid.get_slot(), childid.get_slot());
    }

    // Gets the parent of an entity. if(get_parent()) should be used to check
    // If the parent really exists. Returns falsy value on no parent or invalid id.
    EntityId get_parent(EntityId eid) const
    {
        if(!has(eid))
        {
            ENG_ERROR("Entity {} is invalid", *eid);
            return EntityId{};
        }
        const auto p = hierarchy.get_parent(eid.get_slot());
        if(!p) { return EntityId{}; }
        return entities[*p];
    }

    // Returns true if an entity is a parent to other entity
    bool has_children(EntityId eid) const
    {
        if(!has(eid))
        {
            ENG_ERROR("Entity {} is invalid", *eid);
            return false;
        }
        return (bool)hierarchy.get_first_child(eid.get_slot());
    }

    // Invokes a callback for every child of this entity.
    void loop_over_children(EntityId eid, const auto& callback)
    {
        if(!has(eid))
        {
            ENG_ERROR("Entity {} is invalid", *eid);
            return;
        }
        auto first = hierarchy.get_first_child(eid.get_slot());
        if(!first) { return; }
        auto it = first;
        do
        {
            callback(entities[*it]);
            it = hierarchy.get_next_sibling(it);
        }
        while(it != first);
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
            loop_over_children(id, [&](EntityId id) { self(id, self); });
        };
        traverse(eid, traverse);
    }

    // Register callbacks for new/removed/updated sets of components.
    // Will not run, if entity has only component A, and register_callbacks was ran with A and B.
    template <typename... Components>
    void register_callbacks(std::optional<Callback<ViewEntityInsertedFunc>> on_insert_callback = {},
                            std::optional<Callback<ViewEntityUpdatedFunc>> on_update_callback = {},
                            std::optional<Callback<ViewEntityRemovedFunc>> on_remove_callback = {})
    {
        auto& view = get_view(get_signature<Components...>(), on_insert_callback);
        if(on_insert_callback) { view.on_insert_callbacks += *on_insert_callback; }
        if(on_update_callback) { view.on_update_callbacks += *on_update_callback; }
        if(on_remove_callback) { view.on_remove_callbacks += *on_remove_callback; }
    }

    // Use to notify all callbacks that had at least one of the given component types
    // present during register_callbacks about the update of their contents.
    template <typename... Components> void signal_components_update(EntityId eid)
    {
        notify_entity_views(eid, get_signature<Components...>());
    }

  private:
    EntityMetadata& get_md(EntityId eid) { return metadatas[*eid.get_slot()]; }
    const EntityMetadata& get_md(EntityId eid) const { return metadatas[*eid.get_slot()]; }

    void erase_components(EntityId eid, Signature sig)
    {
        if(!has(eid))
        {
            ENG_ERROR("Invalid entity {}", *eid);
            return;
        }
        auto& md = get_md(eid);
        if((md.sig & sig).none()) { return; }
        for(auto i = 0ull; i < sig.size(); ++i)
        {
            if(sig[i] && md.sig[i]) { pools[i]->erase(eid.get_slot()); }
        }
        const auto newsig = md.sig & (~sig);
        if((md.sig & sig).any()) { on_entity_sig_change(eid, md.sig, newsig); }
        md.sig = newsig;
    }

    template <typename Component> ComponentPool<Component>& get_pool()
    {
        const auto id = ComponentTraits::get_id<Component>();
        if(!pools[id]) { pools[id] = std::make_unique<ComponentPool<Component>>(); }
        return *static_cast<ComponentPool<Component>*>(&*pools[id]);
    }

    View& get_view(Signature sig, std::optional<Callback<ViewEntityInsertedFunc>> on_insert = {})
    {
        auto it = views.emplace(sig, View{});
        if(!it.second) { return it.first->second; }
        View& view = it.first->second;
        view.sig = sig;
        if(auto* pool = try_find_smallest_pool(sig))
        {
            for(auto e : pool->entities)
            {
                if(metadatas[e].has_components(sig))
                {
                    view.entities.push_back(entities[e]);
                    if(on_insert) { (*on_insert)(entities[e]); }
                }
            }
        }
        return view;
    }

    void on_entity_sig_change(EntityId eid, Signature old_sig, Signature new_sig)
    {
        for(auto& [viewsig, view] : views)
        {
            const auto old_sig_passed = view.accepts_signature(old_sig);
            const auto new_sig_passes = view.accepts_signature(new_sig);
            if(!old_sig_passed && new_sig_passes)
            {
                view.entities.push_back(eid);
                view.on_insert_callbacks.signal(eid);
            }
            else if(old_sig_passed && !new_sig_passes)
            {
                const auto rem = std::erase(view.entities, eid);
                ENG_ASSERT(rem == 1);
                view.on_remove_callbacks.signal(eid);
            }
        }
    }

    void notify_entity_views(EntityId eid, Signature updated_comps)
    {
        if(!has(eid)) { return; }
        if(!get_md(eid).has_components(updated_comps))
        {
            ENG_ERROR("The entity does not have the specified components {}", (get_md(eid).sig & updated_comps).to_string());
            return;
        }
        for(auto& [viewsig, view] : views)
        {
            if((updated_comps & view.sig).any()) { view.on_update_callbacks.signal(eid, updated_comps); }
        }
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

    IndexedHierarchy hierarchy;            // manages indices to entities, metadatas and other arrays
    std::vector<EntityId> entities;        // besides indices from hierarchy, stores versions for erase() and has()
    std::vector<EntityMetadata> metadatas; // additional info for entities
    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENTS> pools; // component pools
    std::unordered_map<Signature, View> views;                         // cached entities that have required components
};

} // namespace ecs
} // namespace eng

ENG_DEFINE_STD_HASH(eng::ecs::EntityId, *t);
ENG_DEFINE_STD_HASH(eng::ecs::SlotId, *t);