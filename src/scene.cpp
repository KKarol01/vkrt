#include <algorithm>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <eng/ecs/components.hpp>
#include <eng/scene.hpp>
#include <eng/engine.hpp>
#include <eng/common/logger.hpp>
#include <eng/assets/asset_manager.hpp>
#include <eng/renderer/renderer.hpp>

namespace eng
{

static ecs::Registry& get_ecs() { return *Engine::get().ecs; }

ecs::EntityId Scene::create(std::string_view name, ecs::EntityId parent, const ecsc::Transform& local)
{
    auto& ecs = get_ecs();
    if(parent && !try_get_hierarchy(parent))
    {
        ENG_WARN("Parent entity {} is not in the scene", *parent);
        parent = ecs::EntityId{};
    }

    const auto e = ecs.create();
    if(!e) { return e; }
    ecs.add_components(e, ecsc::Node{ .name = name }, ecsc::Hierarchy{}, ecsc::Transform{ local });
    if(parent) { link(parent, e); }
    else { m_roots_vec.push_back(e); }
    mark_transform_dirty(e);
    return e;
}

ecs::EntityId Scene::instance_asset(const assets::Asset& asset)
{
    ENG_ASSERT(asset.root_nodes.size() > 0);
    if(asset.root_nodes.empty()) { return ecs::EntityId{}; }

    const auto instance_node = [this, &asset](const auto& self, const assets::Node& node, ecs::EntityId parent) -> void {
        const auto local = node.transform != ~0u ? asset.transforms[node.transform] : ecsc::Transform{};
        const auto e = create(node.name, parent, local);
        if(node.meshes.size > 0)
        {
            ecsc::Mesh ecsmesh{};
            ecsmesh.name = "EMPTY NAME";
            ecsmesh.render_meshes = { asset.meshes.begin() + node.meshes.offset,
                                      asset.meshes.begin() + node.meshes.offset + node.meshes.size };
            get_ecs().add_components(e, std::move(ecsmesh));
        }
        for(auto i = 0u; i < node.children.size; ++i)
        {
            self(self, asset.nodes[node.children.offset + i], e);
        }
    };

    // every asset gets a single root
    const auto root = create(ENG_FMT("{}_root", asset.path.filename().string()));
    for(auto rn : asset.root_nodes)
    {
        instance_node(instance_node, asset.nodes[rn], root);
    }
    return root;
}

void Scene::destroy(ecs::EntityId e)
{
    if(!try_get_hierarchy(e))
    {
        ENG_WARN("Entity {} is not in the scene", *e);
        return;
    }
    std::vector<ecs::EntityId> subtree;
    traverse(e, [&subtree](ecs::EntityId id) { subtree.push_back(id); });
    unlink(e);
    // stale ids left in dirty_queue are skipped by update()
    for(auto id : subtree)
    {
        get_ecs().erase(id);
    }
}

void Scene::attach(ecs::EntityId parent, ecs::EntityId child)
{
    if(!try_get_hierarchy(parent) || !try_get_hierarchy(child))
    {
        ENG_WARN("Entities {} and {} must both be in the scene", *parent, *child);
        return;
    }
    for(auto p = parent; p; p = try_get_hierarchy(p)->parent)
    {
        if(p == child)
        {
            ENG_WARN("Cannot parent entity {} to itself or its descendant {}", *child, *parent);
            return;
        }
    }
    if(try_get_hierarchy(child)->parent == parent) { return; }
    unlink(child);
    link(parent, child);
    mark_transform_dirty(child);
}

void Scene::detach(ecs::EntityId e)
{
    const auto* h = try_get_hierarchy(e);
    if(!h || !h->parent) { return; }
    unlink(e);
    m_roots_vec.push_back(e);
    mark_transform_dirty(e);
}

ecs::EntityId Scene::get_parent(ecs::EntityId e) const
{
    const auto* h = try_get_hierarchy(e);
    return h ? h->parent : ecs::EntityId{};
}

bool Scene::has_children(ecs::EntityId e) const
{
    const auto* h = try_get_hierarchy(e);
    return h && h->first_child;
}

void Scene::set_transform(ecs::EntityId e, const ecsc::Transform& local)
{
    if(!get_ecs().has<ecsc::Transform>(e))
    {
        ENG_WARN("Entity {} has no transform", *e);
        return;
    }
    auto& t = get_ecs().get<ecsc::Transform>(e);
    t.position = local.position;
    t.rotation = local.rotation;
    t.scale = local.scale;
    mark_transform_dirty(e);
}

void Scene::set_world_transform(ecs::EntityId e, const glm::mat4& world)
{
    const auto parent = get_parent(e);
    const auto parent_world = parent && get_ecs().has<ecsc::Transform>(parent) ? get_ecs().get<ecsc::Transform>(parent).world
                                                                               : glm::mat4{ 1.0f };
    set_transform(e, ecsc::Transform::init(glm::inverse(parent_world) * world));
}

void Scene::mark_transform_dirty(ecs::EntityId e)
{
    if(!try_get_hierarchy(e)) { return; }
    auto& h = get_ecs().get<ecsc::Hierarchy>(e);
    if(h.transform_dirty) { return; }
    h.transform_dirty = true;
    m_dirty_vec.push_back(e);
}

void Scene::update()
{
    if(m_dirty_vec.empty()) { return; }
    auto& ecs = get_ecs();

    for(auto e : m_dirty_vec)
    {
        if(!ecs.has<ecsc::Hierarchy>(e)) { continue; } // destroyed after being queued
        // A dirty ancestor recomputes this subtree anyway; skipping keeps each entity updated once.
        // Flags are cleared only after the loop, so this holds regardless of queue order.
        bool covered = false;
        auto parent = ecs.get<ecsc::Hierarchy>(e).parent;
        for(auto p = parent; p; p = ecs.get<ecsc::Hierarchy>(p).parent)
        {
            if(ecs.get<ecsc::Hierarchy>(p).transform_dirty)
            {
                covered = true;
                break;
            }
        }
        if(covered) { continue; }
        const auto parent_world =
            parent && ecs.has<ecsc::Transform>(parent) ? ecs.get<ecsc::Transform>(parent).world : glm::mat4{ 1.0f };
        update_subtree(e, parent_world);
    }

    for(auto e : m_dirty_vec)
    {
        if(ecs.has<ecsc::Hierarchy>(e)) { ecs.get<ecsc::Hierarchy>(e).transform_dirty = false; }
    }
    m_dirty_vec.clear();
}

const ecsc::Hierarchy* Scene::try_get_hierarchy(ecs::EntityId e) const
{
    auto& ecs = get_ecs();
    if(!ecs.has<ecsc::Hierarchy>(e)) { return nullptr; }
    return &ecs.get<ecsc::Hierarchy>(e);
}

void Scene::link(ecs::EntityId parent, ecs::EntityId child)
{
    auto& ecs = get_ecs();
    auto& p = ecs.get<ecsc::Hierarchy>(parent);
    auto& c = ecs.get<ecsc::Hierarchy>(child);
    ENG_ASSERT(!c.parent);
    c.parent = parent;
    if(!p.first_child)
    {
        p.first_child = child;
        c.prev_sibling = child;
        c.next_sibling = child;
        return;
    }
    const auto first = p.first_child;
    const auto last = ecs.get<ecsc::Hierarchy>(first).prev_sibling;
    c.next_sibling = first;
    c.prev_sibling = last;
    ecs.get<ecsc::Hierarchy>(last).next_sibling = child;
    ecs.get<ecsc::Hierarchy>(first).prev_sibling = child;
}

void Scene::unlink(ecs::EntityId e)
{
    auto& ecs = get_ecs();
    auto& h = ecs.get<ecsc::Hierarchy>(e);
    if(!h.parent)
    {
        std::erase(m_roots_vec, e);
        return;
    }
    auto& p = ecs.get<ecsc::Hierarchy>(h.parent);
    if(h.next_sibling == e) { p.first_child = ecs::EntityId{}; }
    else
    {
        if(p.first_child == e) { p.first_child = h.next_sibling; }
        ecs.get<ecsc::Hierarchy>(h.prev_sibling).next_sibling = h.next_sibling;
        ecs.get<ecsc::Hierarchy>(h.next_sibling).prev_sibling = h.prev_sibling;
    }
    h.parent = ecs::EntityId{};
    h.prev_sibling = ecs::EntityId{};
    h.next_sibling = ecs::EntityId{};
}

void Scene::update_subtree(ecs::EntityId e, const glm::mat4& parent_world)
{
    auto& ecs = get_ecs();
    auto world = parent_world;
    if(ecs.has<ecsc::Transform>(e))
    {
        auto& t = ecs.get<ecsc::Transform>(e);
        t.world = parent_world * t.to_mat4();
        world = t.world;
        if(ecs.has<ecsc::Mesh>(e)) { gfx::get_renderer().queue_transform_update(e); }
    }
    iterate_children(e, [this, &world](ecs::EntityId c) { update_subtree(c, world); });
}

} // namespace eng
