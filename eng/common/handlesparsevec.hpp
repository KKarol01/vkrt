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
    using storage_t = typename handle_t::storage_type;

  public:
    auto size() const { return data.size() - free_list.size(); }

    T& at(handle_t h) { return data.at(*h); }
    const T& at(handle_t h) const { return data.at(*h); }

    handle_t insert(auto&& e)
    {
        assert(size() < ~storage_t{});
        handle_t handle;
        if(!get_index(handle)) { data.push_back(std::forward<decltype(e)>(e)); }
        else { at(handle) = std::forward<decltype(e)>(e); }
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
            ENG_LOG("REUSING HANDLE {}", *out_handle);
            free_list.pop_front();
            return true;
        }
        out_handle = handle_t{ static_cast<storage_t>(data.size()) };
        return false;
    }

    std::vector<T> data;
    std::deque<handle_t> free_list;
};
} // namespace eng