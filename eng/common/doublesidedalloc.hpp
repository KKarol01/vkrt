#pragma once

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <eng/math/align.hpp>

namespace eng
{
class DoubleSidedAllocator
{
  public:
    DoubleSidedAllocator() = default;
    DoubleSidedAllocator(void* mem, size_t size) : mem(mem), size(size)
    {
        heads[0] = 0;
        heads[1] = 0;
        assert(mem != nullptr && size > 0);
    }

    // calculate size of free space in bytes
    size_t get_free_space() const
    {
        assert(heads[0] <= size - heads[1]);
        return size - heads[1] - heads[0];
    }

    // alloc from left to right (dir = 1) or from right to left (dir = -1)
    void* alloc(size_t req, int dir)
    {
        if(req > get_free_space()) { return nullptr; }
        auto& head = dir == 1 ? heads[0] : heads[1];
        auto* ptr = dir == 1 ? (std::byte*)mem + head : (std::byte*)mem + (size - head - req);
        head += req;
        return ptr;
    }

    void reset(int dir)
    {
        if(dir == 1) { heads[0] = 0; }
        else { heads[1] = 0; }
    }

  private:
    void* mem{};
    size_t size{};
    size_t heads[2]{};
};
} // namespace eng