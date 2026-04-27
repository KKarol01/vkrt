#pragma once

#include <vector>
#include <cstdint>
#include <type_traits>
#include <cstddef>

#include <eng/renderer/renderer_fwd.hpp>
#include <eng/fs/fs.hpp>

namespace eng
{
namespace assets
{
struct Node;
struct Asset;
} // namespace assets
namespace ecs
{
struct Transform;
}

namespace assets
{

// Tries to write bytes. Always increments dst_offset so that it can be used to calculate how many bytes a thing would take, preallocate storage, and be run again.
inline void serialize_write_bytes_safe(void* dst, const void* src, size_t& dst_offset, size_t dst_size, size_t src_size)
{
    if(dst && src && dst_offset + src_size <= dst_size) { std::memcpy((std::byte*)dst + dst_offset, src, src_size); }
    dst_offset += src_size;
}

// Tries to read bytes. Increments src_offset only on success, so number of deserialized bytes can be compared against reference.
inline void deserialize_read_bytes_safe2(void* dst, const void* src, size_t dst_size, size_t& src_offset, size_t src_size)
{
    if(dst && src && src_offset + dst_size <= src_size)
    {
        std::memcpy(dst, (const std::byte*)src + src_offset, dst_size);
        src_offset += dst_size;
    }
};

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
		8B asset byte size			- asset byte size (includes all the dynamic data induced by flags)
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

inline static constexpr size_t HEADER_BYTE_SZ = 4 + 1 + 4;
inline static constexpr size_t LIST_BYTE_SZ = 4 * 8 + 2 * 1;

enum class ListFlags : uint8_t
{
    CONTENT_COMPRESSED_BIT = 1 << 0, // use zlib to decompress
};

struct AssetMetadata
{
    uint64_t uncompressed_size{};
};

struct List
{
    uint64_t custom_hash{};
    uint64_t content_hash{};
    uint64_t asset_start{};
    uint64_t asset_size{}; // this probably could be calculated from total file size or next_item_list_asset_start - this_item_list_asset_start
    uint8_t version{};
    Flags<ListFlags> flags{};
};

struct Container
{
    Container(fs::FilePtr file);

    void read_list_section();

    void add_asset(uint8_t version, uint64_t custom_hash, Flags<ListFlags> flags, std::span<const std::byte> asset,
                   AssetMetadata metadata = {});
    // for streaming compressed data later for the currently added asset, see asset_manager.cpp. set finished on last append to recalc hash.
    void append_asset_bytes(std::span<const std::byte> bytes, bool finished);

    void serialize(size_t& out_bytes_written, std::byte* out_bytes = nullptr, size_t out_bytes_size = 0);
    size_t serialize();

    fs::FilePtr m_file;
    std::vector<List> m_lists;
    std::vector<std::byte> m_asset_bytes;
};

ENG_ENABLE_FLAGS_OPERATORS(ListFlags);

} // namespace v0
} // namespace engb

class Serializer
{
  public:
    template <typename T>
    static void serialize(const T& t, size_t& out_bytes_written, std::byte* out_bytes = nullptr, size_t out_bytes_size = 0)
    {
        static_assert(false, "Add explicit template specialization and provide type serialization routine.");
    }

    template <typename T>
    static void deserialize(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, T& t)
    {
        static_assert(false, "Add explicit template specialization and provide type deserialization routine.");
    }

    template <typename T>
    static void serialize(std::span<const T> t, size_t& out_bytes_written, std::byte* out_bytes = nullptr, size_t out_bytes_size = 0)
    {
        const uint64_t count = t.size();
        serialize_write_bytes_safe(out_bytes, &count, out_bytes_written, out_bytes_size, sizeof(count));
        for(const auto& e : t)
        {
            serialize(e, out_bytes_written, out_bytes, out_bytes_size);
        }
    }

    template <typename T>
        requires(std::is_arithmetic_v<T>)
    static void serialize(std::span<const T> t, size_t& out_bytes_written, std::byte* out_bytes = nullptr, size_t out_bytes_size = 0)
    {
        const uint64_t count = t.size();
        serialize_write_bytes_safe(out_bytes, &count, out_bytes_written, out_bytes_size, sizeof(count));
        serialize_write_bytes_safe(out_bytes, t.data(), out_bytes_written, out_bytes_size, count * sizeof(t[0]));
    }

    static void serialize(std::string_view t, size_t& out_bytes_written, std::byte* out_bytes = nullptr, size_t out_bytes_size = 0)
    {
        const uint64_t count = t.size();
        serialize_write_bytes_safe(out_bytes, &count, out_bytes_written, out_bytes_size, sizeof(count));
        serialize_write_bytes_safe(out_bytes, t.data(), out_bytes_written, out_bytes_size, count);
    }

    template <typename T>
    static void deserialize(const std::byte* out_bytes, size_t& out_bytes_read, size_t out_bytes_size, std::span<T> t)
    {
        const auto elemes_to_read = std::min((out_bytes_size - out_bytes_read) / sizeof(t[0]), t.size());
        for(auto i = 0u; i < elemes_to_read; ++i)
        {
            deserialize(out_bytes, out_bytes_read, out_bytes_size, t[i]);
        }
    }

    template <typename T>
        requires(std::is_arithmetic_v<T>)
    static void deserialize(const std::byte* out_bytes, size_t& out_bytes_read, size_t out_bytes_size, std::span<T> t)
    {
        const auto elemes_to_read = std::min((out_bytes_size - out_bytes_read) / sizeof(t[0]), t.size());
        deserialize_read_bytes_safe2(t.data(), out_bytes, elemes_to_read * sizeof(t[0]), out_bytes_read, out_bytes_size);
    }

    static void deserialize(const std::byte* out_bytes, size_t& out_bytes_read, size_t out_bytes_size, std::string& t)
    {
        uint64_t count;
        deserialize_read_bytes_safe2(&count, out_bytes, sizeof(count), out_bytes_read, out_bytes_size);
        t.resize(count);
        deserialize_read_bytes_safe2(t.data(), out_bytes, count, out_bytes_read, out_bytes_size);
    }

    static void deserialize_resize_vec(auto& vec, const std::byte* out_bytes, size_t& out_bytes_read, size_t out_bytes_size)
    {
        uint64_t count = 0;
        deserialize_read_bytes_safe2(&count, out_bytes, sizeof(count), out_bytes_read, out_bytes_size);
        vec.resize(count);
        deserialize(out_bytes, out_bytes_read, out_bytes_size, std::span{ vec });
    };
};

#define ENG_SERIALIZER_DECLARE(type)                                                                                         \
    template <>                                                                                                              \
    void Serializer::serialize<type>(const type& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size); \
    template <>                                                                                                              \
    void Serializer::deserialize<type>(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, type& t);

ENG_SERIALIZER_DECLARE(assets::Asset);
ENG_SERIALIZER_DECLARE(assets::Node);
ENG_SERIALIZER_DECLARE(gfx::ImageView);
ENG_SERIALIZER_DECLARE(gfx::ParsedImageData);
ENG_SERIALIZER_DECLARE(gfx::ParsedGeometryData);
ENG_SERIALIZER_DECLARE(gfx::Material);
ENG_SERIALIZER_DECLARE(gfx::Mesh);
ENG_SERIALIZER_DECLARE(gfx::Meshlet);
ENG_SERIALIZER_DECLARE(ecs::Transform);
ENG_SERIALIZER_DECLARE(assets::engb::List);

} // namespace assets

} // namespace eng