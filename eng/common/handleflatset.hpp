#pragma once

#include <cstdint>
#include <vector>
#include <concepts>
#include "handle.hpp"

namespace eng
{

template <typename T>
concept FlatSetCompatible = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
    { std::hash<T>{}(a) } -> std::convertible_to<uint64_t>;
};

// Idea stolen from https://github.com/martinus/unordered_dense.
// Hashset, no duplicates, flat (vector) storage, hash and compare, robin hood replacing,
// but with stable addressing and reuse of freed elements
template <FlatSetCompatible T, typename Hash = std::hash<T>, typename EqualTo = std::equal_to<T>> class FlatSet
{
  public:
    using index_t = uint32_t;
    using offset_t = uint32_t;

    inline static constexpr auto MAX_INDEX = ~index_t{};
    inline static constexpr auto MAX_OFFSET = offset_t{ 0xFFFFFF };
    inline static constexpr auto MAX_LOAD = 0.6f;

  private:
    struct Bucket
    {
        bool empty() const { return offset == MAX_OFFSET; }
        void invalidate() { offset = MAX_OFFSET; }
        uint32_t hash : 8 {};
        uint32_t offset : 24 { MAX_OFFSET };
        index_t index{ MAX_INDEX };
    };

  public:
    struct InsertionResult
    {
        index_t index;
        bool success;
    };

    auto begin() { return data.begin(); }
    auto end() { return data.begin() + size(); }

    const T& at(index_t i) const { return data.at(offsets.at(i)); }
    size_t size() const { return head; }

    index_t find(const T& t)
    {
        const auto* b = find_bucket(t);
        if(!b) { return MAX_INDEX; }
        return b->index;
    }

    template <typename Arg>
        requires std::same_as<std::remove_cvref_t<Arg>, std::remove_cvref_t<T>>
    InsertionResult insert(Arg&& t)
    {
        if(size() == MAX_INDEX)
        {
            // can't insert anymore buckets. try searching instead and fail.
            auto* b = find_bucket(t);
            if(b) { return { b->index, false }; }
            return { MAX_INDEX, false };
        }

        maybe_resize();
        const auto hash = Hash{}(t);
        const auto hash8 = uint8_t{ hash & 0xFF };
        auto idx = hash & (buckets.size() - 1);
        Bucket nb{ hash8, 0u, (index_t)offsets.size() };
        Bucket* ob{}; // pointer to new bucket with new item after insertion to update index
        while(true)
        {
            // go over buckets, starting from index calculated from hash
            // and each time increment offset value for robin hood.
            auto& b = buckets.at(idx);
            if(b.empty())
            {
                index_t ret_index{ MAX_INDEX };
                // if the current bucket never had an index allocated.
                // that only happens when the bucket was never used, and
                // will happen only once for each bucket.
                if(b.index == MAX_INDEX)
                {
                    // allocate new index. each bucket has it's own and always keeps it
                    // even after erasing an element from it.
                    // we don't need to set anything for the original bucket,
                    // because it already has the index set to offsets.size()
                    // from before insertion here.
                    offsets.push_back(head);
                    assert(head <= data.size());
                    if(head < data.size()) { data.at(head) = std::forward<Arg>(t); }
                    else { data.emplace_back(std::forward<Arg>(t)); }
                    ret_index = (index_t)offsets.size() - 1;
                }
                // if the bucket already has an index allocated
                else if(b.index < MAX_INDEX)
                {
                    if(!ob) { ob = &nb; } // if not set, we found an empty bucket on the first try.
                    // reuse the index of the found empty bucket.
                    ob->index = b.index;
                    offsets.at(b.index) = head; // point the reused offset at the index to the correct data
                    data.at(head) = std::forward<Arg>(t);
                    ret_index = ob->index;
                }
                else
                {
                    assert(false);
                    return { MAX_INDEX, false };
                }
                b = nb;
                ++head;
                return { ret_index, true };
            }
            // if hash collision, actually compare the items
            if(b.hash == hash8 && EqualTo{}(at(b.index), t)) { return { b.index, false }; }
            // if current bucket has lower offset than ours, swap them
            // and start moving the other one.
            if(b.offset < nb.offset)
            {
                std::swap(b, nb);
                // remember the new bucket with the new element to update the index later
                // as we might find some free bucket with already allocated index.
                if(!ob) { ob = &b; }
            }
            if(nb.offset == MAX_OFFSET)
            {
                rehash(buckets.size() << 1);
                return insert(std::forward<Arg>(t));
            }
            ++nb.offset;
            idx = (idx + 1) & (buckets.size() - 1);
        }
    }

    bool erase(const T& t) { return erase(find_bucket(t)); }

    bool erase(index_t t)
    {
        if(offsets.size() <= t) { return false; }
        auto* b = find_bucket(at(t));
        if(!b) { return false; }

        const auto new_head = head - 1;
        auto* eb = find_bucket(data.at(new_head));
        assert(eb);

        at(t) = std::move(data.at(new_head));
        offsets.at(eb->index) = offsets.at(t);
        --head;

        b->invalidate();
        auto prev = std::distance(buckets.data(), b);
        while(true)
        {
            auto curr = (prev + 1) & (buckets.size() - 1);
            auto& cb = buckets.at(curr);
            if(cb.empty() || cb.offset == 0) { break; }
            --cb.offset;
            buckets.at(prev) = cb;
            cb = Bucket{};
            prev = curr;
        }

        return true;
    }

  private:
    // used only on deleted items
    T& at(index_t i) { return data.at(offsets.at(i)); }

    bool erase(Bucket* b)
    {
        if(!b) { return false; }
        return erase(b->index);
    }

    Bucket* find_bucket(const T& t)
    {
        if(!buckets.size()) { return nullptr; }
        const auto hash = Hash{}(t);
        const auto hash8 = uint8_t{ hash & 0xFF };
        auto idx = hash & (buckets.size() - 1);
        auto offset = 0u;
        while(true)
        {
            auto& b = buckets.at(idx);
            if(b.empty()) { return nullptr; }
            if(b.offset < offset) { return nullptr; } // hash collision sequence has ended
            if(b.hash == hash8 && EqualTo{}(at(b.index), t)) { return &b; }
            ++offset;
            idx = (idx + 1) & (buckets.size() - 1);
        }
    }

    void maybe_resize()
    {
        if(size() < MAX_LOAD * buckets.size()) { return; }
        rehash(std::max(buckets.size() << 1, 1ull));
    }

    void rehash(size_t new_size)
    {
        head = 0;
        buckets.clear();
        buckets.resize(new_size);
        offsets.clear();
        offsets.reserve(data.size());
        auto old = std::move(data);
        data.reserve(old.size());
        for(auto& e : old)
        {
            insert(std::move(e));
        }
    }

    index_t head{}; // linear allocator head for data vector
    std::vector<T> data;
    std::vector<index_t> offsets; // one offset per bucket to index data for stable addressing when deleting items.
    std::vector<Bucket> buckets;
};

template <FlatSetCompatible T, typename Hash = std::hash<T>, typename EqualTo = std::equal_to<T>> class HandleFlatSet
{
    using handle_t = Handle<T>;
    using set_t = FlatSet<T, Hash, EqualTo>;
    using index_t = set_t::index_t;

    struct WrappedInsertionResult
    {
        handle_t handle;
        bool success;
    };

  public:
    auto begin() { return set.begin(); }
    auto end() { return set.end(); }
    const T& at(handle_t h) const { return set.at(*h); }
    handle_t find(const T& t)
    {
        const auto idx = set.find(t);
        if(idx == set.MAX_INDEX) { return handle_t{}; }
        return handle_t{ idx };
    }
    size_t size() const { return set.size(); }

    WrappedInsertionResult insert(auto&& t)
    {
        const auto ret = set.insert(std::forward<decltype(t)>(t));
        return { handle_t{ ret.index }, ret.success };
    }

    bool erase(const T& t) { return set.erase(t); }
    bool erase(handle_t t) { return set.erase(*t); }

  private:
    set_t set;
};

} // namespace eng
