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

class Context;

template <typename T>
concept IsMemcpySafe = std::is_arithmetic_v<T> || std::is_same_v<T, std::byte> || std::is_enum_v<T>;
template <typename T>
concept ImplementsSerialize = requires(const T& ct, T& t, Context& c) {
    ct.serialize(c);
    t.deserialize(c);
};
template <typename T>
concept ImplementsGetStructFields = !ImplementsSerialize<T> && requires { T::get_struct_fields(); };

template <typename T, typename Field> struct StructField
{
    using Type = Field;
    Field T::* fieldptr;
};

class Context
{
  public:
    constexpr Context() = default;
    constexpr Context(std::span<std::byte> bytes, usize offset) : m_bytes(bytes), m_offset(offset) {}

    void safe_write(const void* src, usize src_size)
    {
        if(src && m_offset + src_size <= m_bytes.size()) { std::memcpy(m_bytes.data() + m_offset, src, src_size); }
        m_offset += src_size;
    }

    void safe_read(void* dst, usize dst_size)
    {
        if(dst && m_offset + dst_size <= m_bytes.size()) { std::memcpy(dst, m_bytes.data() + m_offset, dst_size); }
        m_offset += dst_size;
    }

    template <IsMemcpySafe T> void serialize(const T& val) { safe_write(&val, sizeof(T)); }
    template <IsMemcpySafe T> void deserialize(T& val) { safe_read(&val, sizeof(T)); }
    template <ImplementsSerialize T> void serialize(const T& val) { val.serialize(*this); }
    template <ImplementsSerialize T> void deserialize(T& val) { val.deserialize(*this); }
    template <ImplementsGetStructFields T> void serialize(const T& val)
    {
        constexpr auto fields = T::get_struct_fields();
        using TupleType = typename std::decay_t<decltype(fields)>;
        serialize_struct_fields(val, fields, std::make_index_sequence<std::tuple_size_v<TupleType>>{});
    }
    template <ImplementsGetStructFields T> void deserialize(T& val)
    {
        constexpr auto fields = T::get_struct_fields();
        using TupleType = typename std::decay_t<decltype(fields)>;
        deserialize_struct_fields(val, fields, std::make_index_sequence<std::tuple_size_v<TupleType>>{});
    }

    void serialize(const std::string& str)
    {
        const u64 length = static_cast<u64>(str.size());
        safe_write(&length, sizeof(u64));
        safe_write(str.data(), length);
    }
    void deserialize(std::string& str)
    {
        u64 length = 0;
        safe_read(&length, sizeof(u64));
        str.resize(static_cast<usize>(length));
        safe_read(str.data(), static_cast<usize>(length));
    }

    template <typename T> void serialize(const std::vector<T>& vec)
    {
        const u64 count = static_cast<u64>(vec.size());
        safe_write(&count, sizeof(u64));
        serialize(std::span{ vec });
    }
    template <typename T> void deserialize(std::vector<T>& vec)
    {
        u64 count = 0;
        safe_read(&count, sizeof(u64));
        vec.resize(static_cast<usize>(count));
        deserialize(std::span{ vec });
    }

    template <typename T> void serialize(std::span<const T> src)
    {
        if constexpr(IsMemcpySafe<T>) { safe_write(src.data(), src.size_bytes()); }
        else
        {
            for(const auto& e : src)
            {
                serialize(e);
            }
        }
    }
    template <typename T> void deserialize(std::span<T> dst)
    {
        if constexpr(IsMemcpySafe<T>) { safe_read(dst.data(), dst.size_bytes()); }
        else
        {
            for(auto& e : dst)
            {
                deserialize(e);
            }
        }
    }

    template <typename SourceType, typename FieldsTuple, usize... indices>
    void serialize_struct_fields(const SourceType& src, const FieldsTuple& tuple, std::index_sequence<indices...>)
    {
        auto serialize_field = [this](const auto& field) { this->serialize(field); };
        ((serialize_field(src.*std::get<indices>(tuple).fieldptr)), ...);
    }
    template <typename DestType, typename FieldsTuple, usize... indices>
    void deserialize_struct_fields(DestType& dst, const FieldsTuple& tuple, std::index_sequence<indices...>)
    {
        auto deserialize_field = [this](auto& field) { this->deserialize(field); };
        ((deserialize_field(dst.*std::get<indices>(tuple).fieldptr)), ...);
    }

    void serialize(const glm::vec4& vec) { safe_write(&vec, sizeof(vec)); }
    void deserialize(glm::vec4& vec) { safe_read(&vec, sizeof(vec)); }
    template <usize Length> void serialize(const StackString<Length>& str)
    {
        const u64 sz = static_cast<u64>(str.size());
        serialize(sz);
        safe_write(str.c_str(), sz);
    }
    template <usize Length> void deserialize(StackString<Length>& str)
    {
        u64 sz = 0;
        deserialize(sz);
        str.resize(static_cast<usize>(sz));
        safe_read(str.string.data(), static_cast<usize>(sz));
    }
    template <typename T> void serialize(const Flags<T>& flags) { serialize(flags.flags); }
    template <typename T> void deserialize(Flags<T>& flags) { deserialize(flags.flags); }
    template <typename T, typename Storage> void serialize(const Handle<T, Storage>& handle)
    {
        serialize(handle.handle);
    }
    template <typename T, typename Storage> void deserialize(Handle<T, Storage>& handle) { deserialize(handle.handle); }
    template <typename T> void serialize(const Range_T<T>& range)
    {
        serialize(range.offset);
        serialize(range.size);
    }
    template <typename T> void deserialize(Range_T<T>& range)
    {
        deserialize(range.offset);
        deserialize(range.size);
    }

    std::span<std::byte> m_bytes;
    usize m_offset{};
};

// clang-format off
/*
.enbg custom asset byte container format

- 2026.07.12
The format for this is as follows:
{
	4B magic number 'engb',
	1B container spec version,
	3B Padding,
	8B Jump offset to list item count (list section),
	[
		[ optional metadata ]			- e.g., 8B uncompressed size if CONTENT_COMPRESSED_BIT is set
		[ asset bytes ]					- Pointed to by asset_start (metadata lives immediately before this pointer)
	] : ASSET BYTES,

	4B item count in the list of assets in this container,
	[
		{
			8B custom hash for lookup	- usually from virtual path like '/assets/models/model/scene.gltf'
			8B content hash				- hash of the contents
			8B asset start				- Payload-relative offset to the asset bytes (Add N_HEADER_BYTES to get absolute file offset)
			8B asset byte size			- asset byte size (excludes all metadata from flags, offset from it to get them)
			1B version number			- version of the byte representation of the contents in the container
			1B flags					- flags describing the properties of the content
		} : LIST_ITEM
	] : LIST,
} : .ENGB CONTAINER SPEC
*/
// clang-format on
namespace engb
{
inline namespace v0
{

inline static constexpr usize N_HEADER_BYTES = 4 + 1 + 3 + 8;
inline static constexpr usize N_LIST_BYTES = 4 * 8 + 2 * 1;

enum class ListFlags : u8
{
    CONTENT_COMPRESSED_BIT = 1 << 0, // use zlib to decompress
};
ENG_ENABLE_FLAGS_OPERATORS(ListFlags);

// Uncompressed header just before actual asset bytes
struct AssetMetadata
{
    u64 uncompressed_size{};
};

struct List
{
    static constexpr auto get_struct_fields()
    {
        return std::make_tuple(serialization::StructField{ &List::custom_hash }, serialization::StructField{ &List::content_hash },
                               serialization::StructField{ &List::asset_start }, serialization::StructField{ &List::asset_size },
                               serialization::StructField{ &List::version }, serialization::StructField{ &List::flags });
    }
    u64 custom_hash{};
    u64 content_hash{};
    u64 asset_start{}; // start of asset bytes, skipping metadata from flags, which is left uncompressed; does not include N_HEADER_BYTES
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

    void write_to_file();
    void serialize(serialization::Context& ctx) const;
    void deserialize(serialization::Context& ctx);

    std::optional<List> get_asset_list(u64 custom_hash) const;
    usize get_asset_data(const List& list, std::span<std::byte> out_data, usize src_offset) const;

    fs::FilePtr m_file;
    std::vector<List> m_lists_vec;
    std::vector<std::byte> m_asset_bytes;
    bool m_modified{ false };
};

} // namespace v0
} // namespace engb
} // namespace serialization
} // namespace eng