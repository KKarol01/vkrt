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

Container::~Container() { write_to_file(); }

void Container::read_list_section()
{
    if(!m_file || !m_file->is_read())
    {
        ENG_WARN("Couldn't open engb file {} for read", m_file ? m_file->get_path().string() : "<empty path>");
        m_lists_vec.clear();
        return;
    }

    // file is empty, no list to be read
    if(m_file->get_size() == 0) { return; }

    if(m_file->get_size() < N_HEADER_BYTES)
    {
        ENG_WARN("File size ({}) is smaller than engb header size ({})", m_file->get_size(), N_HEADER_BYTES);
        return;
    }

    std::byte header_buf[N_HEADER_BYTES];
    usize read_bytes = 0;
    m_file->read(header_buf, N_HEADER_BYTES, read_bytes, 0);
    if(read_bytes != N_HEADER_BYTES)
    {
        ENG_WARN("Could not read engb header ({})", m_file->get_path().string());
        return;
    }
    if(std::string_view{ (const char*)header_buf, 4 } != "engb")
    {
        ENG_WARN("File is not a valid engb file ({})", m_file->get_path().string());
        return;
    }
    if((char)header_buf[4] != (char)0)
    {
        ENG_WARN("Engb container {} has invalid version {}", m_file->get_path().string(), (char)header_buf[4]);
        return;
    }

    u64 list_offset = 0;
    memcpy(&list_offset, &header_buf[8], sizeof(u64));
    if(list_offset < N_HEADER_BYTES || list_offset > m_file->get_size())
    {
        ENG_WARN("Engb container {} is corrupted (invalid list offset {})", m_file->get_path().string(), list_offset);
        return;
    }

    u32 num_lists = 0;
    m_file->read(reinterpret_cast<std::byte*>(&num_lists), sizeof(u32), read_bytes, list_offset);
    if(num_lists == 0) { return; }

    const usize lists_total_bytes = num_lists * N_LIST_BYTES;
    if(list_offset + sizeof(u32) + lists_total_bytes > m_file->get_size())
    {
        ENG_WARN("Engb container {} is corrupted (list section exceeds file size)", m_file->get_path().string());
        m_lists_vec.clear();
        return;
    }

    std::vector<std::byte> lists_buf(lists_total_bytes);
    usize n_bytes_read = 0;
    m_file->read(lists_buf.data(), lists_total_bytes, n_bytes_read, list_offset + sizeof(u32));
    if(n_bytes_read != lists_total_bytes)
    {
        ENG_WARN("Failed reading engb container lists from file {}", m_file->get_path().string());
        m_lists_vec.clear();
        return;
    }

    m_lists_vec.resize(num_lists);
    serialization::Context ctx{ std::span{ lists_buf }, 0 };
    ctx.deserialize(std::span<List>{ m_lists_vec });

    for(auto& l : m_lists_vec)
    {
        ENG_ASSERT(l.version == 0);
    }
}

void Container::add_asset(u8 version, u64 custom_hash, Flags<ListFlags> flags, std::span<const std::byte> asset,
                          const AssetMetadata& metadata)
{
    m_modified = true;
    m_lists_vec.emplace_back(custom_hash, ENG_HASH(asset), m_asset_bytes.size(), 0, version, flags);

    std::byte buf[64];
    usize metadata_bytes = 0;
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
            ENG_HASH(std::span{ m_asset_bytes.begin() + m_lists_vec.back().asset_start, m_lists_vec.back().asset_size });
    }
}

void Container::write_to_file()
{
    if(!m_modified) { return; }
    if(!m_file)
    {
        ENG_WARN("Cannot serialize engb container: file mode is not permitting writes");
        return;
    }

    const auto n_cont_bytes = N_HEADER_BYTES + m_asset_bytes.size() + sizeof(u32) + (m_lists_vec.size() * N_LIST_BYTES);
    std::vector<std::byte> data(n_cont_bytes);

    serialization::Context ctx(std::span{ data }, 0);
    serialize(ctx);
    ENG_ASSERT(ctx.m_offset == n_cont_bytes);

    usize n_bytes_written = 0;
    m_file->write(data.data(), n_cont_bytes, n_bytes_written, 0);
    ENG_ASSERT(n_bytes_written == n_cont_bytes);
    m_modified = false;
}

void Container::serialize(serialization::Context& ctx) const
{
    ctx.safe_write("engb", 4);
    const u8 version = 0;
    ctx.safe_write(&version, 1);
    const u8 padding[3] = { 0, 0, 0 };
    ctx.safe_write(padding, 3);

    const u64 list_offset = N_HEADER_BYTES + m_asset_bytes.size();
    ctx.safe_write(&list_offset, 8);

    ctx.safe_write(m_asset_bytes.data(), m_asset_bytes.size());

    const u32 n_lists = static_cast<u32>(m_lists_vec.size());
    ctx.safe_write(&n_lists, sizeof(u32));
    ctx.serialize(std::span<const List>{ m_lists_vec });
}

void Container::deserialize(serialization::Context& ctx)
{
    std::byte magic[4];
    ctx.safe_read(magic, 4);
    if(std::string_view{ (const char*)magic, 4 } != "engb")
    {
        ENG_WARN("Invalid magic in engb stream");
        return;
    }
    u8 version = 0;
    ctx.safe_read(&version, 1);
    if(version != 0)
    {
        ENG_WARN("Invalid version in engb stream: {}", version);
        return;
    }
    ctx.m_offset += 3; // 3B padding

    u64 list_offset = 0;
    ctx.safe_read(&list_offset, 8);

    if(list_offset > N_HEADER_BYTES && list_offset <= ctx.m_bytes.size())
    {
        const usize n_asset_bytes = static_cast<usize>(list_offset - N_HEADER_BYTES);
        m_asset_bytes.resize(n_asset_bytes);
        ctx.safe_read(m_asset_bytes.data(), n_asset_bytes);
    }

    ctx.m_offset = static_cast<usize>(list_offset);
    u32 n_lists = 0;
    ctx.safe_read(&n_lists, sizeof(u32));

    m_lists_vec.resize(n_lists);
    ctx.deserialize(std::span<List>{ m_lists_vec });

    for(auto& l : m_lists_vec)
    {
        ENG_ASSERT(l.version == 0 && l.content_hash != 0);
    }
}

std::optional<List> Container::get_asset_list(u64 custom_hash) const
{
    for(const auto& l : m_lists_vec)
    {
        if(l.custom_hash == custom_hash) { return l; }
    }
    return std::nullopt;
}

usize Container::get_asset_data(const List& list, std::span<std::byte> out_data, usize src_offset) const
{
    if(!m_file || !m_file->is_read()) { return 0; }
    if(src_offset >= list.asset_size) { return 0; }
    const usize remaining_in_asset = list.asset_size - src_offset;
    const usize bytes_to_read = std::min(out_data.size(), remaining_in_asset);
    const u64 absolute_file_offset = N_HEADER_BYTES + list.asset_start + src_offset;
    usize n_bytes_read = 0;
    m_file->read(out_data.data(), bytes_to_read, n_bytes_read, absolute_file_offset);
    return n_bytes_read;
}

} // namespace v0
} // namespace engb
} // namespace serialization
} // namespace eng