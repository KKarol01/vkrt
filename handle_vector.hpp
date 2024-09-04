#pragma once

#include <vector>
#include <utility>
#include <algorithm>
#include "handle.hpp"

template <typename T, typename Storage = uint32_t> class HandleVector : private std::vector<T> {
    using Handle = Handle<T, Storage>;
    using Base = std::vector<T>;

  public:
    using Base::back;
    using Base::begin;
    using Base::cbegin;
    using Base::cend;
    using Base::empty;
    using Base::end;
    using Base::front;
    using Base::size;

    template <typename... Args> constexpr Handle emplace_back(Args&&... args) {
        if(!free_slots.empty()) {
            at(free_slots.back()) = T{ std::forward<Args>(args)... };
            Handle h = free_slots.back();
            free_slots.pop_back();
            return h;
        }

        Base::emplace_back(std::forward<Args>(args)...);
        return Handle{ static_cast<Storage>(size() - 1ull) };
    }

    template <typename Val> constexpr Handle push_back(Val&& v) {
        if(!free_slots.empty()) {
            at(free_slots.back()) = std::forward<Val>(v);
            Handle h = free_slots.back();
            free_slots.pop_back();
            return h;
        }

        Base::push_back(std::forward<Val>(v));
        return Handle{ static_cast<Storage>(size() - 1ull) };
    }

    template <typename Container> constexpr Handle insert_range(Container&& cont) {
        const auto _size = size();
        Base::insert_range(std::forward<Container>(cont), end());
        return Handle{ static_cast<Storage>(_size) };
    }

    void erase(Handle h) {
        at(*h).~T();
        free_slots.push_back(h);
    }

    template <class Self> constexpr auto& at(this Self& self, Handle h) { return self.Base::at(*h); }
    template <class Self> constexpr auto& operator[](this Self& self, Handle h) { return self.Base::operator[](*h); }

  private:
    std::vector<Handle> free_slots;
};