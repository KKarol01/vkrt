#pragma once

#include <vector>
#include <cstdint>
#include <type_traits>
#include <cstddef>
#include <tuple>
#include <span>
#include <string>
#include <cstring>
#include <utility>
#include <eng/fs/fs.hpp>
#include <eng/common/flags.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <eng/string/stack_string.hpp>

namespace eng
{

namespace serialization
{

template <typename T, typename Field> struct StructField
{
    using Type = Field;
    Field T::* fieldptr;
};
#define ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(Type, field)                                                            \
    StructField { &Type::field }

// For declaring custom functions to serialize/deserialize a type
#define ENG_SERIALIZATION_DECLARE_CUSTOM_FUNCTIONS(Type)                                                               \
    template <> void serialize<Type>(std::span<std::byte> dst, const Type& src, usize& out_bytes_written);             \
    template <> void deserialize<Type>(Type & dst, std::span<const std::byte> src, usize & out_bytes_written);

// Specialize this function with ENG_SERIALIZATION_REGISTER_FIELDS for a type that you want to be automatically serialized.
template <typename T> inline constexpr auto get_struct_fields() { static_assert(false, "Specialize this function."); }

inline void safe_write(void* dst, const void* src, usize& out_bytes_written, usize dst_size, usize src_size)
{
    if(dst && src && out_bytes_written + src_size <= dst_size)
    {
        std::memcpy((char*)dst + out_bytes_written, src, src_size);
    }
    out_bytes_written += src_size;
}

inline void safe_read(void* dst, const void* src, usize& out_bytes_written, usize dst_size, usize src_size)
{
    if(dst && src && out_bytes_written + dst_size <= src_size)
    {
        std::memcpy(dst, (const char*)src + out_bytes_written, dst_size);
    }
    out_bytes_written += dst_size;
}

template <typename T>
concept MemcpySafe = std::is_arithmetic_v<T> || std::is_same_v<T, std::byte> || std::is_enum_v<T>;

// Forward declare templates so the ones below can be used by those above -- thanks c++
template <typename T> inline void serialize(std::span<std::byte> dst, const T& src, usize& out_bytes_written);
template <typename T> inline void serialize(std::span<std::byte> dst, const Flags<T>& src, usize& out_bytes_written);
template <>
inline void serialize<std::string>(std::span<std::byte> dst, const std::string& src, usize& out_bytes_written);
template <typename T>
inline void serialize(std::span<std::byte> dst, const std::vector<T>& src, usize& out_bytes_written);
template <typename T> inline void serialize(std::span<std::byte> dst, std::span<const T> src, usize& out_bytes_written);
template <usize size>
inline void serialize(std::span<std::byte> dst, const StackString<size>& src, usize& out_bytes_written);
template <typename T> inline void serialize(std::span<std::byte> dst, const Range_T<T>& src, usize& out_bytes_written);
template <typename T, typename Storage>
inline void serialize(std::span<std::byte> dst, const Handle<T, Storage>& src, usize& out_bytes_written);

template <typename T> inline void deserialize(T& dst, std::span<const std::byte> src, usize& out_bytes_written);
template <typename T> inline void deserialize(Flags<T>& dst, std::span<const std::byte> src, usize& out_bytes_written);
template <> inline void deserialize(std::string& dst, std::span<const std::byte> src, usize& out_bytes_written);
template <typename T>
inline void deserialize(std::vector<T>& dst, std::span<const std::byte> src, usize& out_bytes_written);
template <typename T>
inline void deserialize(std::span<T> dst, std::span<const std::byte> src, usize& out_bytes_written);
template <usize size>
inline void deserialize(StackString<size>& dst, std::span<const std::byte> src, usize& out_bytes_written);
template <typename T>
inline void deserialize(Range_T<T>& dst, std::span<const std::byte> src, usize& out_bytes_written);
template <typename T, typename Storage>
inline void deserialize(Handle<T, Storage>& dst, std::span<const std::byte> src, usize& out_bytes_written);

template <typename T, usize... index>
inline void serialize_fields(std::span<std::byte> dst, const T& src, usize& out_bytes_written, std::index_sequence<index...>)
{
    constexpr auto fields = get_struct_fields<T>();
    ((serialize(dst, src.*std::get<index>(fields).fieldptr, out_bytes_written)), ...);
}

template <typename T> inline void serialize(std::span<std::byte> dst, const T& src, usize& out_bytes_written)
{
    if constexpr(MemcpySafe<T>) { safe_write(dst.data(), &src, out_bytes_written, dst.size_bytes(), sizeof(T)); }
    else
    {
        constexpr auto fields = get_struct_fields<T>();
        using TupleType = typename std::decay_t<decltype(fields)>;
        serialize_fields(dst, src, out_bytes_written, std::make_index_sequence<std::tuple_size_v<TupleType>>{});
    }
}

template <typename T> inline void serialize(std::span<std::byte> dst, const Flags<T>& src, usize& out_bytes_written)
{
    serialize(dst, src.flags, out_bytes_written);
}

template <> inline void serialize(std::span<std::byte> dst, const std::string& src, usize& out_bytes_written)
{
    serialize(dst, (u64)src.length(), out_bytes_written);
    serialize(dst, std::span{ src.data(), src.length() }, out_bytes_written);
}

template <typename T>
inline void serialize(std::span<std::byte> dst, const std::vector<T>& src, usize& out_bytes_written)
{
    serialize(dst, src.size(), out_bytes_written);
    serialize(dst, std::span{ src }, out_bytes_written);
}

template <typename T> inline void serialize(std::span<std::byte> dst, std::span<const T> src, usize& out_bytes_written)
{
    if constexpr(MemcpySafe<T>)
    {
        safe_write(dst.data(), src.data(), out_bytes_written, dst.size_bytes(), src.size_bytes());
    }
    else
    {
        for(const auto& e : src)
        {
            serialize(dst, e, out_bytes_written);
        }
    }
}

template <usize size>
inline void serialize(std::span<std::byte> dst, const StackString<size>& src, usize& out_bytes_written)
{
    serialize(dst, (u64)src.size(), out_bytes_written);
    safe_write(dst.data(), src.c_str(), out_bytes_written, dst.size_bytes(), src.size());
}

template <typename T> inline void serialize(std::span<std::byte> dst, const Range_T<T>& src, usize& out_bytes_written)
{
    serialize(dst, src.offset, out_bytes_written);
    serialize(dst, src.size, out_bytes_written);
}

template <typename T, typename Storage>
inline void serialize(std::span<std::byte> dst, const Handle<T, Storage>& src, usize& out_bytes_written)
{
    serialize(dst, src.handle, out_bytes_written);
}

template <typename T, usize... index>
inline void deserialize_fields(T& dst, std::span<const std::byte> src, usize& out_bytes_written, std::index_sequence<index...>)
{
    constexpr auto fields = get_struct_fields<T>();
    ((deserialize(dst.*std::get<index>(fields).fieldptr, src, out_bytes_written)), ...);
}

template <typename T> inline void deserialize(T& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    if constexpr(MemcpySafe<T>) { safe_read(&dst, src.data(), out_bytes_written, sizeof(T), src.size()); }
    else
    {
        constexpr auto fields = get_struct_fields<T>();
        using TupleType = typename std::decay_t<decltype(fields)>;
        deserialize_fields(dst, src, out_bytes_written, std::make_index_sequence<std::tuple_size_v<TupleType>>{});
    }
}

template <typename T> inline void deserialize(Flags<T>& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    deserialize(dst.flags, src, out_bytes_written);
}

template <> inline void deserialize(std::string& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    u64 size;
    deserialize(size, src, out_bytes_written);
    dst.resize(size);
    deserialize(std::span{ dst.data(), size }, src, out_bytes_written);
}

template <typename T>
inline void deserialize(std::vector<T>& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    u64 size;
    deserialize(size, src, out_bytes_written);
    dst.resize(size);
    deserialize(std::span{ dst.data(), dst.size() }, src, out_bytes_written);
}

template <typename T>
inline void deserialize(std::span<T> dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    if constexpr(MemcpySafe<T>)
    {
        safe_read(dst.data(), src.data(), out_bytes_written, dst.size_bytes(), src.size_bytes());
    }
    else
    {
        for(auto& e : dst)
        {
            deserialize(e, src, out_bytes_written);
        }
    }
}

template <usize size>
inline void deserialize(StackString<size>& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    u64 sz;
    deserialize(sz, src, out_bytes_written);
    dst.resize(sz);
    safe_read(dst.string.data(), src.data(), out_bytes_written, sz, src.size_bytes());
}

template <typename T> inline void deserialize(Range_T<T>& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    deserialize(dst.offset, src, out_bytes_written);
    deserialize(dst.size, src, out_bytes_written);
}

template <typename T, typename Storage>
inline void deserialize(Handle<T, Storage>& dst, std::span<const std::byte> src, usize& out_bytes_written)
{
    deserialize(dst.handle, src, out_bytes_written);
}

// clang-format off
/*
.enbg custom asset byte container format

- 2026.04.20
The format for this is as follows:
{
4B magic number 'engb',
1B container spec version,
4B item count in the list of assets in this container,
[
	{
		8B custom hash for lookup	- usually from virtual path like '/assets/models/model/scene.gltf'
		8B content hash				- hash of the contents
		8B asset start				- offset from the beginning of the container to the start of the asset
		8B asset byte size			- asset byte size (excludes all metadata from flags, offset from it to get them)
		1B version number			- version of the byte representation of the contents in the container
		1B flags					- flags describing the properties of the content
	} : LIST_ITEM, ...] : LIST,
	[
		{ [if compressed: 8B size before compression], asset_0_bytes }, 
		...,
		(n-1) asset bytes
	] : ASSET BYTES,
} : .ENGB CONTAINER SPEC
*/
// clang-format on
namespace engb
{
inline namespace v0
{

inline static constexpr usize HEADER_BYTE_SZ = 4 + 1 + 4;
inline static constexpr usize LIST_BYTE_SZ = 4 * 8 + 2 * 1;

enum class ListFlags : u8
{
    CONTENT_COMPRESSED_BIT = 1 << 0, // use zlib to decompress
};

// Uncompressed header just before actual asset bytes
struct AssetMetadata
{
    u64 uncompressed_size{};
};

struct List
{
    u64 custom_hash{};
    u64 content_hash{};
    u64 asset_start{}; // start of asset bytes, skipping metadata from flags, which is left uncompressed
    u64 asset_size{}; // this probably could be calculated from total file size or next_item_list_asset_start - this_item_list_asset_start
    u8 version{};
    Flags<ListFlags> flags{};
};

struct Container
{
    Container(fs::FilePtr file);
    ~Container();

    void read_list_section();

    void add_asset(u8 version, u64 custom_hash, Flags<ListFlags> flags, std::span<const std::byte> asset,
                   const AssetMetadata& metadata = {});
    // for streaming compressed data later for the currently added asset, see asset_manager.cpp. set finished on last append to recalc hash.
    void append_asset_bytes(std::span<const std::byte> bytes, bool finished);

    void serialize();

    std::optional<List> get_asset_list(u64 custom_hash) const;
    usize get_asset_data(const List& list, std::span<std::byte> out_data, usize src_offset) const;

    fs::FilePtr m_file;
    std::vector<List> m_lists_vec;
    std::vector<std::byte> m_asset_bytes;
    bool m_modified{ false };
};

ENG_ENABLE_FLAGS_OPERATORS(ListFlags);

} // namespace v0
} // namespace engb

ENG_SERIALIZATION_DECLARE_CUSTOM_FUNCTIONS(engb::Container);

template <> inline constexpr auto get_struct_fields<engb::List>()
{
    return std::make_tuple(ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(engb::List, custom_hash),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(engb::List, content_hash),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(engb::List, asset_start),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(engb::List, asset_size),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(engb::List, version),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(engb::List, flags));
}

} // namespace serialization

} // namespace eng