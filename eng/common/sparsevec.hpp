//#pragma once
//
//#include <eng/common/handle.hpp>
//#include <eng/common/sparseset.hpp>
//
//template <typename T, typename Hash = std::hash<T>, typename Storage = Handle<T>::Storage_T> class HandleVec
//{
//    using handle_t = Handle<T, Storage>;
//
//  public:
//    auto begin() { return data.begin(); }
//    auto end() { return data.begin() + set.size(); }
//
//    auto size() const { return set.size(); }
//
//    bool has(handle_t handle) const { return set.has(*handle); }
//    bool has(const T& t) const { return has(as_handle(t)); }
//
//    T& at(handle_t h) { return data.at(*h); }
//    const T& at(handle_t h) const { return data.at(*h); }
//
//    handle_t insert(const T& t)
//    {
//        const auto handle = as_handle(t);
//        const auto it = set.insert(handle);
//        if(!it) { return handle; }
//        assert(it.index <= data.size());
//        if(it.index < data.size()) { data.at(it.index) = t; }
//        else { data.push_back(t); }
//        return handle;
//    }
//
//    handle_t insert(T&& t)
//    {
//        const auto handle = as_handle(t);
//        const auto it = set.insert(handle);
//        if(!it) { return handle; }
//        assert(it.index <= data.size());
//        if(it.index < data.size()) { data.at(it.index) = std::move(t); }
//        else { data.push_back(t); }
//        return handle;
//    }
//
//    template <typename... Args> handle_t emplace(Args&&... args) { return insert(T{ std::forward<Args>(args)... }); }
//
//    void erase(handle_t handle)
//    {
//        if(!has(handle)) { return; }
//        const auto it = set.erase(handle);
//        assert(it);
//        data.at(it.index) = std::move(data.at(size()));
//    }
//
//  private:
//    static auto as_hash(const T& t) { return Hash{}(t); }
//    static auto as_handle(const T& t) { return handle_t{ as_hash(t) }; }
//
//    SparseSet set;
//    std::vector<T> data;
//};
