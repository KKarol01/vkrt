#pragma once

#include <new>
#include <cassert>

class FreeListAllocator {
    struct AllocationHeader {
        size_t block_size; // includes allocation header size
        size_t padding;
    };
    struct FreeHeader {
        FreeHeader* next;
        size_t block_size;
    };
    struct PoolHeader {
        FreeHeader* free;
        size_t size;
        size_t used;
    };

  public:
    constexpr FreeListAllocator() = default;

    FreeListAllocator(void* data, size_t size) {
        if(!data) { return; }
        assert(size >= sizeof(PoolHeader) && "Memory pool size too small. Must be at least sizeof(MemoryPool)");

        const size_t header_size_aligned = align_up2(sizeof(PoolHeader), ALIGNMENT);
        const size_t free_size = size - header_size_aligned;
        pool = new (data) PoolHeader{ new (offset_ptr(data, header_size_aligned)) FreeHeader{ nullptr, free_size }, size, 0 };
    }

    void* allocate(size_t size) {
        if(!pool) { return nullptr; }

        const size_t unaligned_alloc = calc_required_size(size);
        const size_t alloc_size = align_up2(unaligned_alloc, ALIGNMENT);
        size_t padding = alloc_size - unaligned_alloc;

        FreeHeader* prev_node = nullptr;
        FreeHeader* free_node = find_first_free(alloc_size, &prev_node);

        if(!free_node) { return nullptr; }

        const size_t remaining = free_node->block_size - alloc_size;

        assert((uintptr_t)free_node % ALIGNMENT == 0);
        assert((uintptr_t)alloc_size % ALIGNMENT == 0);
        assert((uintptr_t)remaining % ALIGNMENT == 0);

        if(remaining >= sizeof(AllocationHeader)) {
            FreeHeader* new_free_node = new (offset_ptr(free_node, alloc_size)) FreeHeader{ nullptr, remaining };
            insert_new_free_block(free_node, new_free_node);
        } else {
            padding += remaining;
        }

        delete_free_block(prev_node, free_node);
        AllocationHeader* alloc = new (free_node) AllocationHeader{ unaligned_alloc, padding };

        pool->used += alloc->block_size + alloc->padding;
        return extract_alloc_data(alloc);
    }

    void deallocate(void* alloc) {
        if(!pool) { return; }
        if(!alloc) { return; }
        assert(alloc >= pool && alloc <= reinterpret_cast<std::byte*>(pool) + pool->size);

        AllocationHeader* header = extract_alloc_header(alloc);
        const size_t free_size = header->block_size + header->padding;
        FreeHeader* free_node = new (header) FreeHeader{ nullptr, free_size };

        FreeHeader* prev_node = nullptr;
        FreeHeader* node = pool->free;
        while(node) {
            if(alloc < node) { break; }
            prev_node = node;
            node = node->next;
        }

        insert_new_free_block(prev_node, free_node);
        assert(!prev_node || (prev_node->next == free_node && free_node->next == node));

        coalesce(prev_node, free_node);
        pool->used -= free_size;
    }

    /*Tries to find best free list size. If allocation is too big for all of them, returns the largest one*/
    size_t try_get_best_fit_size(size_t size) const {
        if(!pool) { return 0; }
        const size_t alloc_size = align_up2(calc_required_size(size), ALIGNMENT);
        FreeHeader* node = pool->free;
        size_t best_size = 0;
        size_t biggest_size = 0;
        while(node) {
            if(node->block_size >= alloc_size && (best_size == 0 || node->block_size < best_size)) {
                best_size = node->block_size;
            }
            if(node->block_size > biggest_size) { biggest_size = node->block_size; }
            node = node->next;
        }

        static constexpr auto max = [](auto a, auto b) { return a > b ? a : b; };

        best_size = max(best_size, sizeof(AllocationHeader)) - sizeof(AllocationHeader);
        biggest_size = max(biggest_size, sizeof(AllocationHeader)) - sizeof(AllocationHeader);

        return best_size > 0 ? best_size : biggest_size;
    }

    static size_t get_alloc_data_size(void* alloc) {
        return extract_alloc_header(alloc)->block_size - sizeof(AllocationHeader);
    }

    size_t get_total_free_memory() const { return pool->size - pool->used - sizeof(PoolHeader); }

    void* release_memory() { return std::exchange(pool, nullptr); }

    size_t get_offset_bytes(const void* alloc) const {
        return reinterpret_cast<uintptr_t>(alloc) - reinterpret_cast<uintptr_t>(pool);
    }

  private:
    void insert_new_free_block(FreeHeader* prev_node, FreeHeader* new_node) {
        if(!prev_node) {
            if(pool->free) {
                assert(new_node < pool->free);
                new_node->next = pool->free;
                pool->free = new_node;
            } else {
                pool->free = new_node;
            }
        } else {
            assert(prev_node < new_node);
            if(!prev_node->next) {
                prev_node->next = new_node;
                new_node->next = nullptr;
            } else {
                new_node->next = prev_node->next;
                prev_node->next = new_node;
            }
        }
    }

    void delete_free_block(FreeHeader* prev_node, FreeHeader* del_node) {
        if(prev_node) {
            prev_node->next = del_node->next;
        } else {
            pool->free = del_node->next;
        }
    }

    void coalesce(FreeHeader* prev_node, FreeHeader* next_node) {
        if(next_node && next_node->next && offset_ptr(next_node, next_node->block_size) == next_node->next) {
            next_node->block_size += next_node->next->block_size;
            delete_free_block(next_node, next_node->next);
        }
        if(prev_node && next_node && offset_ptr(prev_node, prev_node->block_size) == next_node) {
            prev_node->block_size += next_node->block_size;
            delete_free_block(prev_node, next_node);
        }
    }

    FreeHeader* find_first_free(size_t size, FreeHeader** prev_node) const {
        FreeHeader* node = pool->free;
        while(node != NULL) {
            if(node->block_size >= size) { break; }
            *prev_node = node;
            node = node->next;
        }
        return node;
    }

    static size_t calc_required_size(size_t size) { return size + sizeof(AllocationHeader); }

    /* Rounds up to the nearest alignment that is power of two
     equivalent to: size + alignment - (size % alignment)*/
    static size_t align_up2(size_t size, size_t any_pow_2) {
        assert(size > 0);
        assert((any_pow_2 & (any_pow_2 - 1)) == 0 && "any_pow_2 must be non negative power of two");
        return (size + any_pow_2 - 1) & ~(any_pow_2 - 1);
        /*                ^                  ^-- removes the surplus past the nearest next multiple of alignment */
        /*                |--- only overflows to the next multiple if it's not already one of */
    }

    static AllocationHeader* extract_alloc_header(void* alloc) {
        return static_cast<AllocationHeader*>(offset_ptr(alloc, -static_cast<ptrdiff_t>(sizeof(AllocationHeader))));
    }

    static void* extract_alloc_data(AllocationHeader* alloc_header) {
        return offset_ptr(alloc_header, sizeof(AllocationHeader));
    }

    static void* offset_ptr(void* ptr, ptrdiff_t offset) { return static_cast<std::byte*>(ptr) + offset; }

    void validate_free_list_ordering() const {
#ifndef NDEBUG
        FreeHeader* node = pool->free;
        while(node) {
            if(node->next) { assert(node < node->next && "Memory ordering in free lists not preserved"); }
            node = node->next;
        }
#endif // !NDEBUG
    }

    inline static constexpr size_t ALIGNMENT = alignof(max_align_t);
    PoolHeader* pool{ nullptr };
};
