#pragma once

#include <cstdint>
#include <vector>
#include "handle.hpp"

template <typename T, typename Hash, typename EqualTo>
concept HandleFlatSetCompatible = requires(const T& t) {
    { Hash{}(t) } -> std::convertible_to<bool>;
    { EqualTo{}(t, t) } -> std::convertible_to<bool>;
};

// Idea stolen from https://github.com/martinus/unordered_dense.
// Hashset, no duplicates, flat (vector) storage, hash and compare, robin hood replacing,
// but with stable addressing and reuse of freed elements
template <typename Key, typename Storage = Handle<Key>::Storage_T, typename Hash = std::hash<Key>, typename EqualTo = std::equal_to<Key>>
    requires HandleFlatSetCompatible<Key, Hash, EqualTo>
class HandleFlatSet
{
  public:
    using index_t = uint32_t;
    using handle_t = Handle<Key, Storage>;
    static constexpr auto MAX_LOAD_FACTOR = 0.6f;

    struct Bucket
    {
        bool is_empty() const { return psl == ~0u >> 8; }
        void invalidate() { psl = ~0u >> 8; }
        uint32_t psl : 24 { ~0u };
        uint32_t hash : 8 { 0u };
        index_t data{ ~index_t{} };
    };

  public:
    // for actually having dense, iterable, data array, a sparse set architecture would have
    // to be added, adding third vector with indices to data array elements, and making buckets
    // store in data member an index to the element inside indices array, and making data entries
    // a pair of key and index to indices array, allowing swapping the last element in data array
    // with the one to-be-deleted and for updating the indices to data inside indices array. that would
    // add another 8-12 (alignment) bytes for bookeeping, totaling to 16-20 bytes per key.
    // auto begin() { return data.begin(); }
    // auto end() { return data.end(); }
    auto size() const { return data.size(); }

    Key& at(index_t h) { return data.at(h); }
    Key& at(handle_t h) { return data.at(*h); }

    const Key* find(const Key& k) const
    {
        auto it = find_bucket(k);
        if(it == buckets.end()) { return nullptr; }
        return &data.at(it->data);
    }

    template <typename Val> std::pair<handle_t, bool> insert(Val&& v)
    {
        resize();
        const auto hash = Hash{}(v);
        const uint8_t hash8 = hash & 0xFF;
        const auto bs = buckets.size();
        const auto max_psl = Bucket{}.psl;
        auto bi = hash & (bs - 1);
        Bucket cb{ 0, hash8, (index_t)data.size() };
        index_t swapped_at = ~index_t{};
        while(true)
        {
            auto& b = buckets.at(bi);
            if(b.is_empty())
            {
                if(b.data == ~index_t{})
                {
                    b = cb;                                       // overwrite empty bucket
                    data.push_back(std::forward<Val>(v));         // append
                    return { handle_t{ data.size() - 1 }, true }; // return index to last
                }
                else
                {
                    cb.data = b.data;                        // data points to free slot inside data
                    b = cb;                                  // overwrite empty bucket, keeping index to free slot
                    data.at(cb.data) = std::forward<Val>(v); // overwrite destroyed element
                    if(swapped_at != ~index_t{})
                    {
                        // if robin hood happened, the new bucket has default data.size() index, but along the way a free slot was found, so update it's index
                        buckets.at(swapped_at).data = cb.data;
                    }
                    return { handle_t{ cb.data }, true }; // return index to found free slot.
                }
            }
            // check for duplicate
            if(b.hash == hash8 && EqualTo{}(data.at(b.data), v)) { return { handle_t{ b.data }, false }; }
            // robin hood and remember new bucket index in case of finding free slot (not appending means cb's index will erronously point to the end)
            if(b.psl < cb.psl)
            {
                std::swap(b, cb);
                if(swapped_at == ~index_t{}) { swapped_at = bi; }
            }
            bi = (bi + 1) & (bs - 1); // advance with wrap-around
            ++cb.psl;                 // increase offset
            if(cb.psl >= max_psl)
            {
                rehash(bs << 1);
                return insert(std::forward<Val>(v));
            }
        }
    }

    template <typename... Args> std::pair<handle_t, bool> emplace(Args&&... args)
    {
        return insert(Key{ std::forward<Args>(args)... });
    }

    void erase(const Key& k) { backwards_erase(find_bucket(k)); }

    void erase(index_t bi) { erase(data.at(bi)); }

    void reserve(size_t size)
    {
        data.reserve(size);
        rehash(std::bit_ceil((size_t)((float)size / MAX_LOAD_FACTOR)));
    }

  private:
    void resize()
    {
        if(data.size() < buckets.size() * MAX_LOAD_FACTOR) { return; }
        rehash(std::max(buckets.size() << 1, 1ull));
    }

    void rehash(size_t new_size)
    {
        buckets.clear();
        buckets.resize(new_size);
        auto olddata = std::move(data);
        data.reserve(olddata.size());
        for(auto& e : olddata)
        {
            insert(std::move(e));
        }
    }

    auto find_bucket(const Key& k) const
    {
        const auto hash = Hash{}(k);
        const uint8_t hash8 = hash & 0xFF;
        const auto bs = buckets.size();
        auto bi = hash & (bs - 1);
        auto dist = 0ull;
        while(true)
        {
            auto& b = buckets.at(bi);
            if(b.is_empty()) { return buckets.end(); }
            if(b.psl < dist) { return buckets.end(); }
            if(b.hash == hash8 && EqualTo{}(data.at(b.data), k)) { return buckets.begin() + bi; }
            bi = (bi + 1) & (bs - 1);
            ++dist;
        }
    }

    void backwards_erase(const auto it)
    {
        if(it == buckets.end()) { return; }
        auto cur_bi = std::distance(buckets.begin(), it);
        const auto bs = buckets.size();
        buckets.at(cur_bi).invalidate();
        data.at(buckets.at(cur_bi).data).~Key();
        auto next_bi = (cur_bi + 1) & (bs - 1);
        while(true)
        {
            auto& next_b = buckets.at(next_bi);
            if(next_b.is_empty()) { break; }
            if(next_b.psl == 0) { break; }
            --next_b.psl;
            buckets.at(cur_bi) = next_b;
            next_b = Bucket{};
            cur_bi = next_bi;
            next_bi = (next_bi + 1) & (bs - 1);
        }
    }

    std::vector<Key> data;
    std::vector<Bucket> buckets;
};
