#pragma once
#include <cstddef>
#include <cstdint>

class LinearAllocator {
  public:
    LinearAllocator(void* arena, size_t size, uint32_t alignment = alignof(std::max_align_t));

    void* allocate(size_t size, uint32_t alignment = alignof(std::max_align_t));
    void free();
    size_t get_byte_offset(const void* ptr) const;
    size_t get_free_space(uint32_t alignment = alignof(std::max_align_t)) const;

  private:
    void* offset_ptr(void* ptr, ptrdiff_t offset);

    void* unaligned_arena{};
    void* arena{};
    size_t capacity{};
    size_t size{};
};