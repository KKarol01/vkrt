#pragma once
template <class... Ts> struct Visitor : Ts... {
    using Ts::operator()...;
};

struct Range {
    size_t offset{};
    size_t size{};
};