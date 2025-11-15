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
#include <iterator>
#include <tuple>
#include <algorithm>
#include <unordered_map>
#include <eng/common/sparseset.hpp>
#include <eng/common/logger.hpp>
#include <eng/common/callback.hpp>

namespace eng
{
namespace ecs
{
using entity = uint32_t;
using component_id_t = uint32_t;
inline static constexpr entity INVALID_ENTITY = ~entity{};
inline static constexpr entity MAX_COMPONENTS = sizeof(component_id_t) * 8;
using signature_t = std::bitset<MAX_COMPONENTS>;

template <typename... Components> struct View;

struct ComponentIdGenerator
{
    template <typename Component> static component_id_t generate()
    {
        static component_id_t id = _id.fetch_add(1);
        assert(id < MAX_COMPONENTS);
        return id;
    }
    inline static std::atomic<component_id_t> _id{};
};

template <typename Component> component_id_t get_id()
{
    return ComponentIdGenerator::generate<std::remove_cvref_t<Component>>();
}

template <typename Component> component_id_t get_id_bit() { return component_id_t{ 1 } << get_id<Component>(); }

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
            ENG_ERROR("Invalid entity {} for component {}", e, get_id<T>());
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
        components.at(it.index) = std::move(components.back());
        components.pop_back();
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
        signature_t signature{};
        entity parent{ INVALID_ENTITY };
        std::vector<entity> children;
    };

  public:
    struct View
    {
        using on_add_callback_t = void(ecs::entity);
        using on_update_callback_t = void(ecs::entity, signature_t);

        Registry* registry{};
        std::vector<ecs::entity> entities;
        Signal<on_add_callback_t> on_add;
        Signal<on_update_callback_t> on_update;
    };

    // get the components vector to iterate over
    template <typename Component> std::span<const Component> get_components() const
    {
        const auto& arr = get_comp_arr<Component>();
        return std::span{ arr.begin(), arr.end() };
    }

    // creates new entity
    entity create()
    {
        const auto it = entities.insert();
        const auto e = entities.get(it);
        if(e == INVALID_ENTITY)
        {
            ENG_WARN("Max entity reached");
            assert(false);
            return e;
        }
        if(it.index < metadatas.size()) { metadatas.at(it.index) = EntityMetadata{}; }
        else { metadatas.emplace_back(); }
        return e;
    }

    // check if registry has the entity
    bool has(entity e) const { return e != INVALID_ENTITY && entities.has(e); }

    // check if the entity has the components
    template <typename... Components> bool has(entity e) const
    {
        if(is_valid(e))
        {
            const auto mask = signature_t{ (get_id_bit<Components>() | ...) };
            return (get_md(e).signature & mask) == mask;
        }
        return false;
    }

    // get the component from the entity. may return nullptr
    template <typename Component> Component* get(entity e)
    {
        if(!is_valid(e) || !has<Component>(e)) { return nullptr; }
        return static_cast<Component*>(component_arrays.at(get_id<Component>())->get(e));
    }

    // get the component from the entity. may return nullptr
    template <typename Component> const Component* get(entity e) const { return const_cast<Registry*>(this)->get<Component>(e); }

    // attach compontents to the entity
    template <typename... Components> void emplace(entity e, Components&&... comps)
    {
        if(!is_valid(e)) { return; }
        const signature_t compids = (get_id_bit<Components>() | ...);
        get_md(e).signature |= compids;
        ((get_comp_arr<Components>().emplace(e, std::forward<Components>(comps))), ...);
        views_on_add(e, compids);
    }

    // removes the entity, and all of it's children, and all of their components.
    void erase(entity e)
    {
        assert(false);
        // todo: erase from views
        if(!is_valid(e)) { return; }
        auto it = entities.get(e);
        assert(it);
        auto& md = metadatas.at(it.index);
        for(auto i = 0u; i < MAX_COMPONENTS; ++i)
        {
            if(md.signature.test(i)) { component_arrays.at(i)->erase(e); }
        }
        if(md.parent != INVALID_ENTITY) { remove_child(md.parent, e); }
        for(auto c : md.children)
        {
            erase(c);
        }
        auto eraseit = entities.erase(e);
        metadatas.at(eraseit.index) = std::move(metadatas.back());
        metadatas.pop_back();
    }

    // remove the component from the entity.
    template <typename Component> void erase(entity e)
    {
        // todo: remove from views
        if(!is_valid(e)) { return; }
        auto& md = get_md(e);
        const auto idx = get_id<Component>();
        if(md.signature.test(idx))
        {
            component_arrays.at(idx)->erase(e);
            md.signature.reset(idx);
        }
    }

    // get the parent of the entity. INVALID_ENTITY if there is no parent.
    entity get_parent(entity e) const
    {
        if(!is_valid(e)) { return INVALID_ENTITY; }
        return get_md(e).parent;
    }

    // get children of the entity.
    std::span<const entity> get_children(entity e) const
    {
        if(is_valid(e)) { return get_md(e).children; }
        return {};
    }

    // attach a child to a to-be-parent node.
    void make_child(entity p, entity c)
    {
        if(is_valid(p) && is_valid(c))
        {
            auto& pmd = get_md(p);
            auto& cmd = get_md(c);
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

    // remove a child from a parent.
    void remove_child(entity p, entity c)
    {
        if(is_valid(p) && is_valid(c))
        {
            get_md(c).parent = INVALID_ENTITY;
            auto& ch = get_md(p).children;
            const auto it = std::lower_bound(ch.begin(), ch.end(), c);
            if(it != ch.end() && *it == c) { ch.erase(it); }
        }
    }

    // preorder traversal
    void traverse_hierarchy(entity e, const auto& callback)
    {
        if(!is_valid(e)) { return; }
        const auto dfs = [this, &callback](const auto& self, entity p, entity e) -> void {
            callback(p, e);
            auto& ch = get_md(e).children;
            for(auto c : ch)
            {
                self(self, e, c);
            }
        };
        dfs(dfs, INVALID_ENTITY, e);
    }

    // clones entity with it's component and children/parent hierarchy
    entity clone(entity src)
    {
        if(!is_valid(src)) { return INVALID_ENTITY; }
        std::stack<entity> cloned_parents;
        entity root = INVALID_ENTITY;
        traverse_hierarchy(src, [this, &cloned_parents, &root](auto _, auto src_entity) {
            auto dst_entity = create();
            if(root == INVALID_ENTITY) { root = dst_entity; }
            if(cloned_parents.size())
            {
                auto cloned_parent = cloned_parents.top();
                cloned_parents.pop();
                make_child(cloned_parent, dst_entity);
            }
            EntityMetadata& srcmd = get_md(src_entity);
            for(auto i = 0u; i < MAX_COMPONENTS; ++i)
            {
                if(srcmd.signature.test(i))
                {
                    get_md(dst_entity).signature.set(i, true);
                    component_arrays.at(i)->clone(src_entity, dst_entity);
                }
            }
            for(auto i = 0u; i < srcmd.children.size(); ++i)
            {
                cloned_parents.push(dst_entity);
            }
            views_on_add(dst_entity, srcmd.signature);
        });
        assert(cloned_parents.empty());
        return root;
    }

    template <typename... Components> void update(entity e)
    {
        const auto sig = signature_t{ (get_id_bit<Components>() | ...) };
        for(auto i = 0u; i < viewsigs.size(); ++i)
        {
            const auto& vsig = viewsigs.at(i);
            if((sig & vsig).any()) { views.at(i).on_update.signal(e, sig); }
        }
    }

    // obtains a view of the only entities that have the specified components.
    // optionally, takes callbacks that are called when entities get removed or added.
    template <typename... Components>
    ecs::View<Components...> get_view(std::optional<Callback<View::on_add_callback_t>> on_add = std::nullopt,
                                      std::optional<Callback<View::on_update_callback_t>> on_update = std::nullopt)
    {
        const auto sig = signature_t{ (get_id_bit<Components>() | ...) };
        View* rview{};
        for(auto i = 0u; i < viewsigs.size(); ++i)
        {
            if(sig == viewsigs.at(i))
            {
                rview = &views.at(i);
                break;
            }
        }
        if(!rview)
        {
            viewsigs.push_back(sig);
            rview = &views.emplace_back();
            rview->registry = this;
            for(auto i = 0u; i < entities.size(); ++i)
            {
                auto e = entities.at(i);
                const auto& md = metadatas.at(i);
                if((sig & md.signature) == sig) { rview->entities.push_back(e); }
            }
        }
        if(on_add) { rview->on_add += *on_add; }
        if(on_update) { rview->on_update += *on_update; }
        return ecs::View<Components...>{ rview };
    }

  private:
    // checks if entity is valid and is registered.
    bool is_valid(entity e) const { return e != INVALID_ENTITY && has(e); }

    template <typename Component, typename CompNoRef = std::remove_cvref_t<Component>> auto& get_comp_arr()
    {
        auto& comparr = component_arrays.at(get_id<CompNoRef>());
        if(!comparr) { comparr = std::make_unique<ComponentPool<CompNoRef>>(); }
        return *static_cast<ComponentPool<CompNoRef>*>(&*comparr);
    }

    // broadcasts to views new entity or new component.
    // mask is used to skip views that already contain this entity.
    // mask should only contain bits of newly added components.
    void views_on_add(entity e, signature_t mask = signature_t{})
    {
        if(e == INVALID_ENTITY) { return; }
        const auto& md = get_md(e);
        const auto prevsig = md.signature ^ mask;
        for(auto i = 0u; i < viewsigs.size(); ++i)
        {
            const auto sig = viewsigs.at(i);
            // only add an entity, when before it couldn't pass the view's signature
            // due to lacking required components, and now it can.
            if((prevsig & sig) != sig && (md.signature & sig) == sig)
            {
                auto& view = views.at(i);
                view.entities.push_back(e);
                view.on_add.signal(e);
            }
        }
    }

    EntityMetadata& get_md(entity e) { return metadatas.at(entities.get(e).index); }
    const EntityMetadata& get_md(entity e) const { return metadatas.at(entities.get(e).index); }

    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENTS> component_arrays;
    std::deque<View> views;
    std::vector<signature_t> viewsigs; // corresponds 1:1 to views
    SparseSet entities;
    std::vector<EntityMetadata> metadatas; // corresponds 1:1 to entities in dense array
};

template <typename... Components> struct View
{
    struct iterator
    {
        using difference_type = ptrdiff_t;
        using value_type = std::tuple<entity, Components...>;
        using pointer = std::tuple<entity, std::add_pointer_t<Components>...>;
        using reference = std::tuple<entity, std::add_lvalue_reference_t<Components>...>;
        using iterator_category = std::random_access_iterator_tag;

        // clang-format off
        iterator(Registry::View* rview, ecs::entity* e) : rview(rview), e(e) {}
        bool operator==(iterator it) const { return rview == it.rview && e == it.e; }
        bool operator!=(iterator it) const { return !(*this == it); }
        iterator& operator++() { ++e; return *this; }
        iterator& operator--() { --e; return *this; }
        reference operator*() { return reference{ *e, *rview->registry->get<Components>(*e)... }; }
        reference operator->() { return iterator::operator*(); }
        // clang-format on

        Registry::View* rview{};
        entity* e{};
    };

    auto begin() { return iterator{ rview, rview->entities.data() }; }
    auto end() { return iterator{ rview, rview->entities.data() + rview->entities.size() }; }
    auto size() const { return rview->entities.size(); }

    Registry::View* rview{};
};

} // namespace ecs
} // namespace eng
