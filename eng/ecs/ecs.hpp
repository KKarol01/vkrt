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
        assert(_id.load() < MAX_COMPONENTS);
        static component_id_t id = _id++;
        return id;
    }
    template <typename Component> static component_id_t generate_bit()
    {
        return component_id_t{ 1 } << generate<Component>();
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
        using callback_t = void(ecs::entity);

        Registry* registry{};
        std::vector<ecs::entity> entities;
        Signal<callback_t> on_add;
    };

    template <typename Component> std::span<const Component> get_components()
    {
        const auto& arr = get_comp_arr<Component>();
        return std::span{ arr.begin(), arr.end() };
    }

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
        if(it.index < vmetadatas.size()) { vmetadatas.at(it.index) = EntityMetadata{}; }
        else { vmetadatas.emplace_back(); }
        return e;
    }

    bool has(entity e) const { return entities.has(e); }

    template <typename Component> bool has(entity e) const
    {
        if(is_valid(e))
        {
            const auto idx = ComponentIdGenerator::generate<Component>();
            return get_md(e).signature.test(idx);
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

    template <typename... Components> void emplace(entity e, Components&&... comps)
    {
        if(!is_valid(e)) { return; }
        const signature_t compids = ((ComponentIdGenerator::generate_bit<std::remove_cvref_t<Components>>()) | ...);
        get_md(e).signature |= compids;
        ((get_comp_arr<std::remove_cvref_t<Components>>().emplace(e, std::forward<Components>(comps))), ...);
        views_on_add(e, compids);
    }

    void erase(entity e)
    {
        assert(false);
        // todo: erase from views
        if(!is_valid(e)) { return; }
        auto it = entities.get(e);
        assert(it);
        auto& md = vmetadatas.at(it.index);
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
        vmetadatas.at(eraseit.index) = std::move(vmetadatas.back());
        vmetadatas.pop_back();
    }

    template <typename Component> void erase(entity e)
    {
        // todo: remove from views
        if(!is_valid(e)) { return; }
        auto& md = get_md(e);
        const auto idx = ComponentIdGenerator::generate<Component>();
        if(md.signature.test(idx))
        {
            component_arrays.at(idx)->erase(e);
            md.signature.reset(idx);
        }
    }

    entity get_parent(entity e) const
    {
        if(!is_valid(e)) { return INVALID_ENTITY; }
        return get_md(e).parent;
    }

    std::span<const entity> get_children(entity e) const
    {
        if(is_valid(e)) { return get_md(e).children; }
        return {};
    }

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
            EntityMetadata& emd = get_md(src_entity);
            for(auto i = 0u; i < MAX_COMPONENTS; ++i)
            {
                if(emd.signature.test(i))
                {
                    get_md(dst_entity).signature.set(i, true);
                    component_arrays.at(i)->clone(src_entity, dst_entity);
                }
            }
            for(auto i = 0u; i < emd.children.size(); ++i)
            {
                cloned_parents.push(dst_entity);
            }
            views_on_add(dst_entity, emd.signature);
        });
        assert(cloned_parents.empty());
        return root;
    }

    template <typename... Components>
    ecs::View<Components...> get_view(std::optional<Callback<View::callback_t>> on_add = std::nullopt)
    {
        const signature_t sig = ((ComponentIdGenerator::generate_bit<Components>()) | ...);
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
                const auto& md = vmetadatas.at(i);
                if((sig & md.signature) == sig) { rview->entities.push_back(e); }
            }
        }
        if(on_add) { rview->on_add += *on_add; }
        return ecs::View<Components...>{ rview };
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

    // broadcasts to views new entity or new component.
    // mask is used to skip views that already contain this entity.
    // mask should contain bits of newly added components.
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

    EntityMetadata& get_md(entity e) { return vmetadatas.at(entities.get(e).index); }
    const EntityMetadata& get_md(entity e) const { return vmetadatas.at(entities.get(e).index); }

    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENTS> component_arrays;
    std::deque<View> views;
    std::vector<signature_t> viewsigs; // corresponds 1:1 to views
    SparseSet entities;
    std::vector<EntityMetadata> vmetadatas;
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

    Registry::View* rview{};
};

} // namespace ecs
} // namespace eng
