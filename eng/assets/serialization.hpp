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
struct Asset;
}
namespace ecs
{
struct Transform;
}

namespace assets
{

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
        return {};
    }

    template <typename T>
    static void deserialize(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, T& t)
    {
        static_assert(false, "Add explicit template specialization and provide type deserialization routine.");
        return {};
    }
};

#define ENG_SERIALIZER_DECLARE(type)                                                                                         \
    template <>                                                                                                              \
    void Serializer::serialize<type>(const type& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size); \
    template <>                                                                                                              \
    void Serializer::deserialize<type>(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, type& t);

ENG_SERIALIZER_DECLARE(assets::Asset);
ENG_SERIALIZER_DECLARE(gfx::ImageView);
ENG_SERIALIZER_DECLARE(ecs::Transform);
ENG_SERIALIZER_DECLARE(assets::engb::List);

} // namespace assets

} // namespace eng