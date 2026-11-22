#pragma once
#include <string_view>
#include <vector>
#include <eng/common/types.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{

namespace assets
{
struct Asset;
}

/*
    Scene owns the entity hierarchy (ecsc::Hierarchy) and world transforms (ecsc::Transform::world).

    Every entity in the scene has ecsc::Node, ecsc::Hierarchy and ecsc::Transform.
    Transform holds the local TRS; world is derived from it.

    Changing a local transform queues the entity. update() recomputes world matrices
    for every queued entity and its subtree, parents first, once per frame.

    Destroy scene entities with destroy(), not ecs->erase(), so links stay valid.
*/
class Scene
{
  public:
    // Creates an entity with Node, Hierarchy and Transform components. Optionally parents it.
    ecs::EntityId create(std::string_view name, ecs::EntityId parent = ecs::EntityId{},
                         const ecsc::Transform& local = ecsc::Transform{});

    ecs::EntityId instance_asset(const assets::Asset& asset);

    // Destroys the entity and its whole subtree.
    void destroy(ecs::EntityId e);

    // Set parent-child relationship
    void attach(ecs::EntityId parent, ecs::EntityId child);
    // Unset parent-child relationship
    void detach(ecs::EntityId e);

    ecs::EntityId get_parent(ecs::EntityId e) const;
    bool has_children(ecs::EntityId e) const;
    const std::vector<ecs::EntityId>& get_root_entities() const { return m_roots_vec; }

    template <typename Callback> void iterate_children(ecs::EntityId e, const Callback& callback) const;
    // Traverse hierarchy depth-first.
    template <typename Callback> void traverse(ecs::EntityId e, const Callback& callback) const;

    // Sets the local transform and queues a world update.
    void set_transform(ecs::EntityId e, const ecsc::Transform& local);
    // Sets the local transform so that the resulting world matrix equals given one.
    void set_world_transform(ecs::EntityId e, const glm::mat4& world);
    // Queues a world update after modifying the local Transform directly.
    void mark_transform_dirty(ecs::EntityId e);

    // Recomputes world matrices of queued entities and their subtrees.
    void update();

  private:
    const ecsc::Hierarchy* try_get_hierarchy(ecs::EntityId e) const;
    // Appends an unlinked (no parent, not in roots) child to parent's children.
    void link(ecs::EntityId parent, ecs::EntityId child);
    // Removes from parent's children, or from roots.
    void unlink(ecs::EntityId e);
    void update_subtree(ecs::EntityId e, const glm::mat4& parent_world);

    std::vector<ecs::EntityId> m_roots_vec;
    std::vector<ecs::EntityId> m_dirty_vec;
};

template <typename Callback> void Scene::iterate_children(ecs::EntityId e, const Callback& callback) const
{
    const auto* h = try_get_hierarchy(e);
    if(!h || !h->first_child) { return; }
    const auto first = h->first_child;
    auto child = first;
    do
    {
        // fetch next before the callback, so it may safely detach the child
        const auto next = try_get_hierarchy(child)->next_sibling;
        callback(child);
        child = next;
    }
    while(child && child != first);
}

template <typename Callback> void Scene::traverse(ecs::EntityId e, const Callback& callback) const
{
    if(!try_get_hierarchy(e)) { return; }
    const auto recurse = [this, &callback](const auto& self, ecs::EntityId id) -> void {
        callback(id);
        iterate_children(id, [&self](ecs::EntityId c) { self(self, c); });
    };
    recurse(recurse, e);
}

} // namespace eng
