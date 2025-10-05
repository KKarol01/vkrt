#pragma once

#include <cassert>
#include <vector>
#include <deque>
#include <eng/common/handle.hpp>

namespace eng
{
template <typename T> class HandleSparseVec
{
    using handle_t = Handle<T>;
    using Storage = typename handle_t::Storage_T;

  public:
    auto size() const { return data.size() - free_list.size(); }

    T& at(handle_t h) { return data.at(*h); }
    const T& at(handle_t h) const { return data.at(*h); }

    handle_t insert(const T& t)
    {
        assert(size() < ~Storage{});
        handle_t handle;
        if(!get_index(handle)) { data.push_back(t); }
        else { at(handle) = t; }
        return handle;
    }

    handle_t insert(T&& t)
    {
        assert(size() < ~Storage{});
        handle_t handle;
        if(!get_index(handle)) { data.push_back(std::move(t)); }
        else { at(handle) = std::move(t); }
        return handle;
    }

    template <typename... Args> handle_t emplace(Args&&... args)
    {
        assert(size() < ~Storage{});
        handle_t handle;
        if(!get_index(handle)) { data.emplace_back(std::forward<Args>(args)...); }
        else { at(handle) = T{ std::forward<Args>(args)... }; }
        return handle;
    }

    void erase(handle_t handle)
    {
        assert(handle);
        if(!handle) { return; }
        at(handle).~T();
        free_list.push_back(handle);
    }

  private:
    bool get_index(handle_t& out_handle)
    {
        if(free_list.size())
        {
            out_handle = free_list.front();
            free_list.pop_front();
            return true;
        }
        out_handle = handle_t{ static_cast<Storage>(data.size()) };
        return false;
    }

    std::vector<T> data;
    std::deque<handle_t> free_list;
};
} // namespace eng