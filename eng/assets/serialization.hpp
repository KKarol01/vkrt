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
		8B asset byte size			- asset byte size
		8B asset start				- offset from the beginning of the container to the start of the asset
		1B version number			- version of the byte representation of the contents in the container
	} : LIST_ITEM, ...] : LIST,
	[asset_0_bytes, asset_1_bytes, ..., asset_item_count_bytes] : ASSET BYTES,
} : .ENGB CONTAINER SPEC
*/
// clang-format on

namespace engb
{
inline namespace v0
{

struct Container
{
    inline static constexpr size_t HEADER_BYTE_SZ = 4 + 1 + 4;
    inline static constexpr size_t LIST_BYTE_SZ = 25;

    struct List
    {
        uint64_t custom_hash{};
        uint64_t content_hash{};
        uint64_t asset_start{};
        uint8_t version{};
    };

    void add_asset(uint8_t version, uint64_t custom_hash, std::span<const std::byte> asset);

    void serialize(size_t& out_bytes_written, std::byte* out_bytes = nullptr, size_t out_bytes_size = 0);
    void serialize(size_t& out_bytes_written, fs::FilePtr out_file);

    std::vector<List> lists;
    std::vector<std::byte> asset_bytes;
};

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

} // namespace assets

} // namespace eng