#pragma once

#include <cassert>
#include <vector>
#include <functional>

namespace eng
{

template <typename Func> using Callback = std::function<Func>;

template <typename Func> class Signal
{
  public:
    void subscribe(auto&& f) { callbacks.emplace_back(std::forward<decltype(f)>(f)); }
    Signal& operator+=(auto&& f)
    {
        subscribe(std::forward<decltype(f)>(f));
        return *this;
    }
    void send(auto&&... args)
    {
        for(const auto& e : callbacks)
        {
            e(std::forward<decltype(args)>(args)...);
        }
    }
    std::vector<Callback<Func>> callbacks;
};

} // namespace eng