#pragma once

#include <compare>
#include <cstdint>
#include <cassert>
#include <vector>
#include <variant>
#include <type_traits>
#include <eng/common/logger.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/slotallocator.hpp>

class IndexedHierarchy
{
    struct Node;

  public:
    using NodeId = eng::TypedId<Node, uint32_t>;

  private:
    struct Node
    {
        bool is_single_child() const { return prev_sibling == next_sibling; }
        NodeId parent{};       // not false if has parent
        NodeId first_child{};  // not false if has children
        NodeId prev_sibling{}; // not false if has parent
        NodeId next_sibling{}; // not false if has parent
    };

  public:
    bool has(NodeId id) const { return id && slots.has(*id); }

    uint32_t size() const { return slots.size(); }

    NodeId create()
    {
        if(slots.size() == ~NodeId::storage_type{}) { return NodeId{}; }
        const auto slot = slots.allocate();
        if(nodes.size() == slot) { nodes.emplace_back(); }
        return NodeId{ slot };
    }

    void make_child(NodeId parentid, NodeId childid)
    {
        if(!has(parentid) || !has(childid) || parentid == childid)
        {
            assert(false);
            return;
        }

        Node& parent = get(parentid);
        Node& child = get(childid);

        ENG_ASSERT(!child.parent); // not implemented
        child.parent = parentid;

        if(!parent.first_child)
        {
            parent.first_child = childid;
            child.next_sibling = childid;
            child.prev_sibling = childid;
        }
        else
        {
            NodeId first = parent.first_child;
            NodeId last = get(first).prev_sibling;
            child.next_sibling = first;
            child.prev_sibling = last;
            get(last).next_sibling = childid;
            get(first).prev_sibling = childid;
        }
    }

    // Unparents node and removes from sibling chain
    void detach(NodeId id)
    {
        if(!has(id)) { return; }
        Node& child = get(id);
        if(!child.parent) { return; }
        Node& parent = get(child.parent);
        if(child.is_single_child()) { parent.first_child = {}; }
        else
        {
            if(parent.first_child == id) { parent.first_child = child.next_sibling; }
            get(child.prev_sibling).next_sibling = child.next_sibling;
            get(child.next_sibling).prev_sibling = child.prev_sibling;
        }
        child.parent = {};
        child.next_sibling = {};
        child.prev_sibling = {};
    }

    // Removes the node from the hierarchy and unparents it's children, and breaks their siblings relation.
    void erase(NodeId id)
    {
        if(!has(id)) { return; }
        detach(id);
        if(auto fc = get_first_child(id))
        {
            auto child = fc;
            do
            {
                auto& node = get(child);
                child = get_next_sibling(child);
                node.parent = {};
                node.next_sibling = {};
                node.prev_sibling = {};
            }
            while(child != fc);
        }
        nodes[*id] = {};
        slots.erase(*id);
    }

    // Return false if no parent
    NodeId get_parent(NodeId id) const { return get(id).parent; }

    // Return false if no children
    NodeId get_first_child(NodeId id) const { return get(id).first_child; }

    // Siblings are circular. false If child has no parent
    NodeId get_next_sibling(NodeId id) const { return get(id).next_sibling; }

    template <std::invocable<NodeId> Callback> void traverse_hierarchy(NodeId id, const Callback& callback) const
    {
        const auto recursive = [this, &callback](NodeId id, const auto& self) -> void {
            if(!has(id)) { return; }
            callback(id);
            const auto first_child = get_first_child(id);
            if(!first_child) { return; }
            auto child = first_child;
            do
            {
                self(child, self);
                child = get_next_sibling(child);
            }
            while(child != first_child);
        };
        recursive(id, recursive);
    }

  private:
    Node& get(NodeId id)
    {
        if(!has(id))
        {
            assert(false);
            return null_object;
        }
        return nodes[*id];
    }

    const Node& get(NodeId id) const { return const_cast<IndexedHierarchy*>(this)->get(id); }

    inline static Node null_object = Node{ NodeId{}, NodeId{}, NodeId{}, NodeId{} };
    SlotAllocator slots;
    std::vector<Node> nodes;
};