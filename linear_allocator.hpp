#pragma once
#include <cstddef>
#include <cstdint>
#ifndef NDEBUG
#include <cassert>
#endif

class LinearAllocator {
  public:
    LinearAllocator(void* arena, size_t size, uint32_t alignment = alignof(std::max_align_t)) {
        const size_t aligned = align_up2(reinterpret_cast<uintptr_t>(arena), alignment);
        const size_t diff = aligned - reinterpret_cast<uintptr_t>(arena);
        if(diff >= size) { return; }
        this->unaligned_arena = arena;
        this->arena = offset_ptr(arena, diff);
        capacity = size - diff;
    }
    void* allocate(size_t size, uint32_t alignment = alignof(std::max_align_t)) {
        if(!arena) { return nullptr; }
        if(get_free_space(alignment) < size) { return nullptr; }
        const size_t aligned_offset = align_up2(this->size, alignment);
        const size_t padding = aligned_offset - this->size;
        const size_t padded_size = size + padding;
        if(get_free_space(1) < padded_size) { return nullptr; }
        void* alloc = offset_ptr(arena, aligned_offset);
        this->size += padded_size;
        return alloc;
    }

    void free() { size = 0; }
    size_t get_byte_offset(const void* ptr) const {
        assert(unaligned_arena <= ptr && ptr <= (std::byte*)unaligned_arena + capacity);
        return reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(unaligned_arena);
    }
    size_t get_free_space(uint32_t alignment = alignof(std::max_align_t)) const {
        return align_down2(capacity - size, alignment);
    }

  private:
    static size_t align_up2(size_t val, size_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        return (val + (alignment - 1ull)) & (~(alignment - 1ull));
    }
    static size_t align_down2(size_t val, size_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        return val & ~(alignment - 1ull);
    }

    void* offset_ptr(void* ptr, ptrdiff_t offset) { return static_cast<std::byte*>(ptr) + offset; }

    void* unaligned_arena{};
    void* arena{};
    size_t capacity{};
    size_t size{};
};