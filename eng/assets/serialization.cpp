#include "serialization.hpp"
#include <eng/engine.hpp>
#include <eng/assets/asset_manager.hpp>
#include <eng/common/hash.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{
namespace serialization
{
namespace engb
{
namespace v0
{

Container::Container(fs::FilePtr file) : m_file(file) { read_list_section(); }

Container::~Container() { serialize(); }

void Container::read_list_section()
{
    if(!m_file || !m_file->is_read())
    {
        ENG_WARN("Couldn't open engb file {} for read", m_file ? m_file->path().string() : "<empty path>");
        m_lists_vec.clear();
        return;
    }

    // file is empty, no list to be read
    if(m_file->size() == 0) { return; }

    std::byte buf[64];
    static_assert(LIST_BYTE_SZ <= std::size(buf));
    auto read_bytes = m_file->read(buf, HEADER_BYTE_SZ, 0);
    if(read_bytes != HEADER_BYTE_SZ)
    {
        ENG_WARN("Could read engb header ({})", m_file->path().string());
        return;
    }
    if(std::string_view{ (const char*)buf, 4 } != "engb")
    {
        ENG_WARN("File is not valid engb file ({})", m_file->path().string());
        return;
    }

    if((char)buf[4] != (char)0)
    {
        ENG_WARN("Engb container {} has invalid version {}", m_file->path().string(), (char)buf[4]);
        return;
    }

    uint32_t num_lists;
    memcpy(&num_lists, &buf[5], sizeof(uint32_t));
    m_lists_vec.reserve(num_lists);
    for(auto i = 0u; i < num_lists; ++i)
    {
        auto& l = m_lists_vec.emplace_back();
        const auto file_read = m_file->read(buf, LIST_BYTE_SZ);
        if(file_read != LIST_BYTE_SZ)
        {
            ENG_WARN("Failed reading engb container list from file {}", m_file->path().string());
            m_lists_vec.clear();
            return;
        }
        size_t out_bytes_written = 0;
        serialization::deserialize(l, std::span{ buf }, out_bytes_written);
        ENG_ASSERT(l.version == 0);
    }
}

void Container::add_asset(uint8_t version, uint64_t custom_hash, Flags<ListFlags> flags,
                          std::span<const std::byte> asset, const AssetMetadata& metadata)
{
    m_modified = true;
    m_lists_vec.emplace_back(custom_hash, ENG_HASH(asset), m_asset_bytes.size(), 0, version, flags);

    std::byte buf[64];
    size_t metadata_bytes = 0;
    if(flags & ListFlags::CONTENT_COMPRESSED_BIT)
    {
        ENG_ASSERT(metadata.uncompressed_size > 0);
        memcpy(buf, &metadata.uncompressed_size, sizeof(metadata.uncompressed_size));
        metadata_bytes += sizeof(metadata.uncompressed_size);
    }
    ENG_ASSERT(metadata_bytes <= std::size(buf));

    m_asset_bytes.insert(m_asset_bytes.end(), buf, buf + metadata_bytes);
    m_asset_bytes.insert(m_asset_bytes.end(), asset.begin(), asset.end());
    m_lists_vec.back().asset_start += metadata_bytes;
    m_lists_vec.back().asset_size = asset.size();
}

void Container::append_asset_bytes(std::span<const std::byte> bytes, bool finished)
{
    m_asset_bytes.insert(m_asset_bytes.end(), bytes.begin(), bytes.end());
    m_lists_vec.back().asset_size += bytes.size();
    if(finished)
    {
        m_lists_vec.back().content_hash =
            ENG_HASH(ENG_HASH_AS_SPAN(m_asset_bytes.begin() + m_lists_vec.back().asset_start, m_lists_vec.back().asset_size));
    }
}

void Container::serialize()
{
    if(!m_modified) { return; }
    if(!m_file)
    {
        ENG_WARN("Cannot serialize engb container: file mode is not permitting writes");
        return;
    }
    const auto total_size_bytes = HEADER_BYTE_SZ + (m_lists_vec.size() * LIST_BYTE_SZ) + 8 + m_asset_bytes.size();
    std::vector<std::byte> data(total_size_bytes);
    size_t out_bytes_written = 0;
    serialization::serialize(std::span{ data }, *this, out_bytes_written);
    ENG_ASSERT(total_size_bytes == out_bytes_written);
    const auto file_bytes_writen = m_file->write(data.data(), out_bytes_written, 0, true);
    ENG_ASSERT(file_bytes_writen == total_size_bytes);
}

std::optional<List> Container::get_asset_list(uint64_t custom_hash) const
{
    for(const auto& l : m_lists_vec)
    {
        if(l.custom_hash == custom_hash) { return l; }
    }
    return std::nullopt;
}

size_t Container::get_asset_data(const List& list, std::span<std::byte> out_data, size_t src_offset) const
{
    if(!m_file || !m_file->is_read()) { return 0; }
    if(src_offset >= list.asset_size) { return 0; }
    const size_t remaining_in_asset = list.asset_size - src_offset;
    const size_t bytes_to_read = std::min(out_data.size(), remaining_in_asset);
    const auto file_bytes = m_file->read(out_data.data(), bytes_to_read, list.asset_start + src_offset);
    return file_bytes;
}

} // namespace v0
} // namespace engb

template <>
void serialize<engb::Container>(std::span<std::byte> dst, const engb::Container& src, size_t& out_bytes_written)
{
    static constexpr const char* MAGIC = "engb";
    static constexpr uint8_t VERSION = 0;
    auto list = src.m_lists_vec;
    serialize(dst, std::span{ MAGIC, 4 }, out_bytes_written);
    serialize(dst, VERSION, out_bytes_written);
    serialize(dst, (uint32_t)list.size(), out_bytes_written);
    for(auto& l : list)
    {
        l.asset_start = engb::HEADER_BYTE_SZ + (src.m_lists_vec.size() * engb::LIST_BYTE_SZ) + 8 + l.asset_start;
        serialize(dst, l, out_bytes_written);
    }
    serialize(dst, src.m_asset_bytes, out_bytes_written);
}

template <>
void deserialize<engb::Container>(engb::Container& dst, std::span<const std::byte> src, size_t& out_bytes_written)
{
    char magic[4]{};
    uint8_t version{};
    uint32_t count{};
    deserialize(std::span{ magic, 4 }, src, out_bytes_written);
    deserialize(version, src, out_bytes_written);
    deserialize(count, src, out_bytes_written);
    dst.m_lists_vec.resize(count);
    deserialize(std::span{ dst.m_lists_vec }, src, out_bytes_written);
    deserialize(dst.m_asset_bytes, src, out_bytes_written);
}

} // namespace serialization
} // namespace eng