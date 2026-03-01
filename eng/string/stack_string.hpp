#pragma once

#include <string>
#include <string_view>
#include <cstring>
#include <cstdint>
#include <array>
#include <eng/common/hash.hpp>

namespace eng
{

template <size_t size> struct StackString
{
    static_assert(size > 0);
    StackString() = default;

    StackString(const StackString& a) noexcept = default;
    StackString& operator=(const StackString& a) noexcept = default;
    StackString(StackString&& a) noexcept = default;
    StackString& operator=(StackString&& a) noexcept = default;

    StackString(const char* cstr) { *this = cstr; }
    StackString& operator=(const char* cstr)
    {
        if(!cstr) { return *this; }
        std::string_view view{ cstr };
        const auto length = std::min(view.length(), size - 1);
        memcpy(string.data(), cstr, length);
        string[length] = '\0';
        return *this;
    }

    template <size_t other_size> bool operator==(const StackString<other_size>& other) const
    {
        return as_view() == other.as_view();
    }

    std::string_view as_view() const { return std::string_view{ string.data() }; }
    std::string to_string() const { return std::string{ string.data() }; }
    const char* c_str() const { return string.data(); }

    uint64_t hash() const { return ENG_HASH_STR(string.data()); }

    std::array<char, size> string{};
};

} // namespace eng

namespace std
{
template <size_t size> struct hash<eng::StackString<size>>
{
    size_t operator()(const eng::StackString<size>& t) const { return (size_t)t.hash(); }
};
} // namespace std
