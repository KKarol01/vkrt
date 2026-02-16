#pragma once

#include <compare>
#include <cstdint>
#include <cassert>
#include <vector>
#include <variant>
#include <eng/common/logger.hpp>
#include <eng/common/handle.hpp>

template <typename UserType> class IndexedHierarchy
{
  public:
    struct element_id_t;
    using element_id = eng::TypedIntegral<element_id_t, uint32_t>;

  private:
    struct Element
    {
        inline static Element null_object{};
        bool is_single_child() const { return prev_sibling == next_sibling; }
        bool is_used() const { return data.index() != 0; }
        element_id as_next_free() const { return std::get<0>(data); }
        UserType& as_user() { return std::get<1>(data); }
        const UserType& as_user() const { return std::get<1>(data); }
        std::variant<element_id, UserType> data;
        element_id parent{};       // not ~0 if has parent
        element_id first_child{};  // not ~0 if has children
        element_id prev_sibling{}; // not ~0 if has parent
        element_id next_sibling{}; // not ~0 if has parent
    };

  public:
    UserType& at(element_id index) { return elements[*index].as_user(); }
    const UserType& at(element_id index) const { return elements[*index].as_user(); }

    template <typename... Args> element_id insert(Args&&... args)
    {
        if(next_free)
        {
            const auto idx = next_free;
            auto& elem = elements[*idx];
            assert(!elem.is_used());
            next_free = elem.as_next_free();
            elem.data = UserType{ std::forward<Args>(args)... };
            return idx;
        }
        elements.push_back(Element{ UserType{ std::forward<Args>(args)... } });
        return element_id{ (typename element_id::storage_type)elements.size() - 1 };
    }

    void make_child(element_id parentid, element_id childid)
    {
        if(!parentid || !childid || parentid == childid)
        {
            assert(false);
            return;
        }

        Element* parent = &get(parentid);
        Element* child = &get(childid);

        ENG_ASSERT(!child->parent); // not implemented
        child->parent = parentid;

        if(!parent->first_child)
        {
            parent->first_child = childid;
            child->next_sibling = childid;
            child->prev_sibling = childid;
        }
        else
        {
            element_id first = parent->first_child;
            element_id last = get(first).prev_sibling;
            child->next_sibling = first;
            child->prev_sibling = last;
            get(last).next_sibling = childid;
            get(first).prev_sibling = childid;
        }
    }

    // Unparents element and removes it's siblings
    void detach(element_id element)
    {
        if(!element) { return; }
        Element& child = get(element);
        if(!child.parent) { return; }
        Element& parent = get(child.parent);
        if(child.is_single_child()) { parent.first_child = {}; }
        else
        {
            if(parent.first_child == element) { parent.first_child = child.next_sibling; }
            get(child.prev_sibling).next_sibling = child.next_sibling;
            get(child.next_sibling).prev_sibling = child.prev_sibling;
        }
        child.parent = {};
        child.next_sibling = {};
        child.prev_sibling = {};
    }

    // Removes the element from the hierarchy and destroys the object
    void erase(element_id element)
    {
        if(!element) { return; }
        detach(element);
        auto& elem = elements[*element];
        elem = {};
        elem.data = next_free;
        next_free = element;
    }

    // Return false if no parent
    element_id get_parent(element_id element) const { return get(element).parent; }

    // Return false if no children
    element_id get_first_child(element_id element) const { return get(element).first_child; }

    // Siblings are circular. false If child has no parent
    element_id get_next_sibling(element_id element) const { return get(element).next_sibling; }

  private:
    Element& get(element_id index)
    {
        if(!index) { return Element::null_object; }
        return elements[*index];
    }
    const Element& get(element_id index) const
    {
        if(!index) { return Element::null_object; }
        return elements[*index];
    }

    std::vector<Element> elements;
    element_id next_free{};
};