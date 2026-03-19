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

    StackString(const char* str) { *this = str; }
    StackString(const std::string& str) { *this = str; }
    StackString(std::string_view str) { *this = str; }
    StackString& operator=(const char* str) { return (*this = std::string_view{ str }); }
    StackString& operator=(const std::string& str) { return (*this = std::string_view{ str.begin(), str.end() }); }
    StackString& operator=(std::string_view str)
    {
        const auto length = std::min(str.length(), size - 1);
        memcpy(string.data(), str.data(), length);
        string[length] = '\0';
        return *this;
    }

    template <size_t other_size> auto operator<=>(const StackString<other_size>& other) const
    {
        return as_view() <=> other.as_view();
    }
    template <size_t other_size> bool operator==(const StackString<other_size>& other) const
    {
        return as_view() == other.as_view();
    }

    auto operator<=>(const char* other) const { return as_view() <=> std::string_view{ other }; }
    bool operator==(const char* other) const { return as_view() == std::string_view{ other }; }

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
