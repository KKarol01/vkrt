#include "serialization.hpp"

#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/assets/asset_manager.hpp>
#include <eng/common/hash.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{
namespace assets
{
namespace engb
{
namespace v0
{

Container::Container(fs::FilePtr file) : m_file(file)
{
    if(!m_file || !m_file->is_read())
    {
        ENG_WARN("Engb container file {} is empty or cannot be read", file ? file->path.string() : "<empty path>");
        return;
    }
    read_list_section();
}

void Container::read_list_section()
{
    if(!m_file || !m_file->is_open())
    {
        ENG_WARN("Couldn't open engb file {} for read", m_file ? m_file->path.string() : "<empty path>");
        m_lists_vec.clear();
        return;
    }

    // file is empty, no list to be read
    if(m_file->size == 0) { return; }

    std::byte buf[1024 * 8];
    auto read_bytes = m_file->read(buf, HEADER_BYTE_SZ);
    if(read_bytes != HEADER_BYTE_SZ)
    {
        ENG_WARN("Could read engb header ({})", m_file->path.string());
        return;
    }
    if(std::string_view{ (const char*)buf, 4 } != "engb")
    {
        ENG_WARN("File is not valid engb file ({})", m_file->path.string());
        return;
    }

    if((char)buf[4] != (char)0)
    {
        ENG_WARN("Engb container {} has invalid version {}", m_file->path.string(), (char)buf[4]);
        return;
    }

    uint32_t num_lists;
    memcpy(&num_lists, &buf[5], sizeof(uint32_t));
    m_lists_vec.reserve(num_lists);
    uint32_t remaining = num_lists;
    constexpr size_t lists_per_buf = std::size(buf) / LIST_BYTE_SZ;
    while(remaining > 0)
    {
        const size_t lists_to_read = std::min((size_t)remaining, lists_per_buf);
        const size_t bytes_to_read = lists_to_read * LIST_BYTE_SZ;
        const auto file_read = m_file->read(buf, bytes_to_read);
        if(file_read != bytes_to_read)
        {
            ENG_WARN("Failed to read engb container {} list section", m_file->path.string());
            return;
        }
        size_t bytes_processed = 0;
        for(size_t i = 0; i < lists_to_read; ++i)
        {
            auto& list = m_lists_vec.emplace_back();
            Serializer::deserialize(buf, bytes_processed, bytes_to_read, list);
        }
        remaining -= lists_to_read;
    }
}

void Container::add_asset(uint8_t version, uint64_t custom_hash, Flags<ListFlags> flags, std::span<const std::byte> asset)
{
    m_lists_vec.emplace_back(custom_hash, ENG_HASH(asset), m_asset_bytes.size(), 0, version, flags);

    // std::byte buf[64];
    // size_t metadata_bytes = 0;
    //  if(flags & ListFlags::CONTENT_COMPRESSED_BIT)
    //{
    //      ENG_ASSERT(metadata.uncompressed_size > 0);
    //      memcpy(buf, &metadata.uncompressed_size, sizeof(metadata.uncompressed_size));
    //      metadata_bytes += sizeof(metadata.uncompressed_size);
    //  }

    // ENG_ASSERT(metadata_bytes <= std::size(buf));
    // m_asset_bytes.insert(m_asset_bytes.end(), buf, buf + metadata_bytes);
    m_asset_bytes.insert(m_asset_bytes.end(), asset.begin(), asset.end());
    // m_lists_vec.back().asset_start += metadata_bytes;
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

void Container::serialize(size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    static constexpr const char* MAGIC = "engb";
    static constexpr uint8_t VERSION = 0;

    serialize_write_bytes_safe(out_bytes, MAGIC, out_bytes_written, out_bytes_size, 4);
    serialize_write_bytes_safe(out_bytes, &VERSION, out_bytes_written, out_bytes_size, 1);

    const uint32_t item_count = (uint32_t)m_lists_vec.size();
    serialize_write_bytes_safe(out_bytes, &item_count, out_bytes_written, out_bytes_size, 4);
    for(auto& l : m_lists_vec)
    {
        l.asset_start = HEADER_BYTE_SZ + (m_lists_vec.size() * LIST_BYTE_SZ) + l.asset_start;
        Serializer::serialize(l, out_bytes_written, out_bytes, out_bytes_size);
    }

    serialize_write_bytes_safe(out_bytes, m_asset_bytes.data(), out_bytes_written, out_bytes_size, m_asset_bytes.size());
}

size_t Container::serialize()
{
    size_t out_bytes_written = 0;
    if(!m_file || !m_file->is_write())
    {
        ENG_WARN("Cannot serialize engb container: file mode is not permitting writes");
        return out_bytes_written;
    }
    const auto total_size_bytes = HEADER_BYTE_SZ + (m_lists_vec.size() * LIST_BYTE_SZ) + m_asset_bytes.size();
    std::vector<std::byte> data(total_size_bytes);
    serialize(out_bytes_written, data.data(), total_size_bytes);
    ENG_ASSERT(total_size_bytes == out_bytes_written);
    const auto file_bytes_writen = m_file->write(data.data(), out_bytes_written);
    ENG_ASSERT(file_bytes_writen == total_size_bytes);
    m_file->file.flush();
    return out_bytes_written;
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
void Serializer::serialize<assets::Asset>(const assets::Asset& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    if(t.geometry_data.empty())
    {
        ENG_ASSERT(false,
                   "Geometry data must not be empty when serializing. Did you forget to provide required keep data "
                   "flag when importing?");

        return;
    }

    const auto pathstr = t.path.string();
    serialize(std::string_view{ pathstr }, out_bytes_written, out_bytes, out_bytes_size);

    ENG_ASSERT(t.images.size() == t.image_data.size());
    serialize(std::span{ t.image_data }, out_bytes_written, out_bytes, out_bytes_size);

    const uint64_t texture_count = t.textures.size();
    serialize_write_bytes_safe(out_bytes, &texture_count, out_bytes_written, out_bytes_size, sizeof(texture_count));
    for(auto txt : t.textures)
    {
        // store index to image array so it can later be deserialized
        *txt.image = (uint32_t)std::distance(t.images.begin(), std::ranges::find(t.images, txt.image));
        serialize(txt, out_bytes_written, out_bytes, out_bytes_size);
    }

    ENG_ASSERT(t.geometries.size() == t.geometry_data.size());
    const uint64_t geometry_count = t.geometry_data.size();
    serialize_write_bytes_safe(out_bytes, &geometry_count, out_bytes_written, out_bytes_size, sizeof(geometry_count));
    for(const auto& gdf : t.geometry_data_futures)
    {
        const auto& gd = gdf.get();
        serialize(gd, out_bytes_written, out_bytes, out_bytes_size);
    }

    const uint64_t material_count = t.materials.size();
    serialize_write_bytes_safe(out_bytes, &material_count, out_bytes_written, out_bytes_size, sizeof(material_count));
    for(const auto& math : t.materials)
    {
        auto mat = math.get();
        // remap image handles to store indices to t.images array, so they can be safely deserialized.
        if(mat.base_color_texture)
        {
            *mat.base_color_texture.image =
                (uint32_t)std::distance(t.images.begin(), std::ranges::find(t.images, mat.base_color_texture.image));
        }
        if(mat.normal_texture)
        {
            *mat.normal_texture.image =
                (uint32_t)std::distance(t.images.begin(), std::ranges::find(t.images, mat.normal_texture.image));
        }
        if(mat.metallic_roughness_texture)
        {
            *mat.metallic_roughness_texture.image =
                (uint32_t)std::distance(t.images.begin(), std::ranges::find(t.images, mat.metallic_roughness_texture.image));
        }
        mat.mesh_pass = {}; // ignoring meshpass, as load_material uses default one
        serialize(mat, out_bytes_written, out_bytes, out_bytes_size);
    }

    const uint64_t mesh_count = t.meshes.size();
    serialize_write_bytes_safe(out_bytes, &mesh_count, out_bytes_written, out_bytes_size, sizeof(mesh_count));
    for(const auto& meshh : t.meshes)
    {
        auto mesh = meshh.get();
        *mesh.geometry = (uint32_t)std::distance(t.geometries.begin(), std::ranges::find(t.geometries, mesh.geometry));
        if(mesh.material)
        {
            *mesh.material = (uint32_t)std::distance(t.materials.begin(), std::ranges::find(t.materials, mesh.material));
        }
        serialize(mesh, out_bytes_written, out_bytes, out_bytes_size);
    }

    serialize(std::span{ t.nodes }, out_bytes_written, out_bytes, out_bytes_size);
    serialize(std::span{ t.transforms }, out_bytes_written, out_bytes, out_bytes_size);
    serialize(std::span{ t.root_nodes }, out_bytes_written, out_bytes, out_bytes_size);
}

template <>
void Serializer::deserialize<assets::Asset>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, assets::Asset& t)
{
    std::string pathstr;
    deserialize(bytes, out_bytes_read, out_bytes_size, pathstr);
    t.path = pathstr;
    ENG_ASSERT(!t.path.empty())

    uint64_t image_count;
    deserialize_read_bytes_safe2(&image_count, bytes, sizeof(image_count), out_bytes_read, out_bytes_size);
    t.images.resize(image_count);
    gfx::ParsedImageData imgd{};
    for(auto i = 0u; i < image_count; ++i)
    {
        // don't use deserialize() here, because it would double the image data for no reason, we can read from the stream
        uint64_t pixel_data_size = 0;
        deserialize_read_bytes_safe2(&imgd.width, bytes, sizeof(imgd.width), out_bytes_read, out_bytes_size);
        deserialize_read_bytes_safe2(&imgd.height, bytes, sizeof(imgd.height), out_bytes_read, out_bytes_size);
        deserialize_read_bytes_safe2(&imgd.format, bytes, sizeof(imgd.format), out_bytes_read, out_bytes_size);
        deserialize_read_bytes_safe2(&pixel_data_size, bytes, sizeof(pixel_data_size), out_bytes_read, out_bytes_size);
        t.images[i] = get_engine().renderer->make_image("EMPTY IMAGE NAME",
                                                        gfx::Image::init(imgd.width, imgd.height, 0, imgd.format,
                                                                         gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                             gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                         0, 1, gfx::ImageLayout::READ_ONLY));
        const auto imgdata_start = out_bytes_read;
        out_bytes_read += pixel_data_size;
        if(!t.images[i])
        {
            ENG_WARN("Failed to create image {}", "EMPTY IMAGE NAME");
            continue;
        }
        else
        {
            gfx::get_renderer().staging->copy(t.images[i].get(), bytes + imgdata_start, 0, 0, true, gfx::DiscardContents::YES);
            ENG_TODO("TODO: Process mips");
        }
    }

    uint64_t texture_count;
    deserialize_read_bytes_safe2(&texture_count, bytes, sizeof(texture_count), out_bytes_read, out_bytes_size);
    t.textures.resize(texture_count);
    for(auto& txt : t.textures)
    {
        deserialize(bytes, out_bytes_read, out_bytes_size, txt);
        txt.image = t.images[*txt.image]; // when serializing, handles are made into indices into this array
    }

    uint64_t geom_count;
    deserialize_read_bytes_safe2(&geom_count, bytes, sizeof(geom_count), out_bytes_read, out_bytes_size);
    t.geometries.resize(geom_count);
    for(uint64_t i = 0; i < geom_count; ++i)
    {
        gfx::ParsedGeometryData geom;
        deserialize(bytes, out_bytes_read, out_bytes_size, geom);
        t.geometries[i] =
            gfx::get_renderer().make_geometry(gfx::GeometryDescriptor{ .flags = {},
                                                                       .vertex_layout = geom.vertex_layout,
                                                                       .index_format = gfx::IndexFormat::U16,
                                                                       .vertices = geom.positions,
                                                                       .attributes = geom.attributes,
                                                                       .indices = std::as_bytes(std::span{ geom.indices }),
                                                                       .meshlets = geom.meshlets });
    }

    uint64_t material_count;
    deserialize_read_bytes_safe2(&material_count, bytes, sizeof(material_count), out_bytes_read, out_bytes_size);
    t.materials.resize(material_count);
    for(uint64_t i = 0; i < material_count; ++i)
    {
        gfx::Material mat;
        deserialize(bytes, out_bytes_read, out_bytes_size, mat);
        //if(mat.base_color_texture) { mat.base_color_texture.image = t.images[*mat.base_color_texture.image]; }
        //if(mat.normal_texture) { mat.normal_texture.image = t.images[*mat.normal_texture.image]; }
        //if(mat.metallic_roughness_texture)
        //{
        //    mat.metallic_roughness_texture.image = t.images[*mat.metallic_roughness_texture.image];
        //}
        t.materials[i] = gfx::get_renderer().make_material(mat);
    }

    uint64_t mesh_count = 0;
    deserialize_read_bytes_safe2(&mesh_count, bytes, sizeof(mesh_count), out_bytes_read, out_bytes_size);
    t.meshes.resize(mesh_count);
    for(uint64_t i = 0; i < mesh_count; ++i)
    {
        gfx::Mesh mesh;
        deserialize(bytes, out_bytes_read, out_bytes_size, mesh);
        if(mesh.material) { mesh.material = t.materials[*mesh.material]; }
        if(mesh.geometry) { mesh.geometry = t.geometries[*mesh.geometry]; }
        t.meshes[i] = gfx::get_renderer().make_mesh(gfx::MeshDescriptor{ mesh.geometry, mesh.material });
    }

    deserialize_resize_vec(t.nodes, bytes, out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.transforms, bytes, out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.root_nodes, bytes, out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<assets::Node>(const assets::Node& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize(std::string_view{ t.name }, out_bytes_written, out_bytes, out_bytes_size);
    serialize_write_bytes_safe(out_bytes, &t.meshes, out_bytes_written, out_bytes_size, sizeof(t.meshes));
    serialize_write_bytes_safe(out_bytes, &t.transform, out_bytes_written, out_bytes_size, sizeof(t.transform));
    serialize_write_bytes_safe(out_bytes, &t.parent, out_bytes_written, out_bytes_size, sizeof(t.parent));
    serialize_write_bytes_safe(out_bytes, &t.children, out_bytes_written, out_bytes_size, sizeof(t.children));
}

template <>
void Serializer::deserialize<assets::Node>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, assets::Node& t)
{
    deserialize(bytes, out_bytes_read, out_bytes_size, t.name);
    deserialize_read_bytes_safe2(&t.meshes, bytes, sizeof(t.meshes), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.transform, bytes, sizeof(t.transform), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.parent, bytes, sizeof(t.parent), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.children, bytes, sizeof(t.children), out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<gfx::ImageView>(const gfx::ImageView& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize_write_bytes_safe(out_bytes, &t.image, out_bytes_written, out_bytes_size, sizeof(t.image));
    serialize_write_bytes_safe(out_bytes, &t.type, out_bytes_written, out_bytes_size, sizeof(t.type));
    serialize_write_bytes_safe(out_bytes, &t.format, out_bytes_written, out_bytes_size, sizeof(t.format));
    serialize_write_bytes_safe(out_bytes, &t.src_subresource, out_bytes_written, out_bytes_size, sizeof(t.src_subresource));
    serialize_write_bytes_safe(out_bytes, &t.dst_subresource, out_bytes_written, out_bytes_size, sizeof(t.dst_subresource));
}

template <>
void Serializer::deserialize<gfx::ImageView>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::ImageView& t)
{
    deserialize_read_bytes_safe2(&t.image, bytes, sizeof(t.image), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.type, bytes, sizeof(t.type), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.format, bytes, sizeof(t.format), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.src_subresource, bytes, sizeof(t.src_subresource), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.dst_subresource, bytes, sizeof(t.dst_subresource), out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<gfx::ParsedImageData>(const gfx::ParsedImageData& t, size_t& out_bytes_written,
                                                 std::byte* out_bytes, size_t out_bytes_size)
{
    serialize_write_bytes_safe(out_bytes, &t.width, out_bytes_written, out_bytes_size, sizeof(t.width));
    serialize_write_bytes_safe(out_bytes, &t.height, out_bytes_written, out_bytes_size, sizeof(t.height));
    serialize_write_bytes_safe(out_bytes, &t.format, out_bytes_written, out_bytes_size, sizeof(t.format));
    const uint64_t byte_size = t.data.size();
    serialize_write_bytes_safe(out_bytes, &byte_size, out_bytes_written, out_bytes_size, sizeof(byte_size));
    serialize_write_bytes_safe(out_bytes, t.data.data(), out_bytes_written, out_bytes_size, byte_size);
}

template <>
void Serializer::deserialize<gfx::ParsedImageData>(const std::byte* bytes, size_t& out_bytes_read,
                                                   size_t out_bytes_size, gfx::ParsedImageData& t)
{
    deserialize_read_bytes_safe2(&t.width, bytes, sizeof(t.width), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.height, bytes, sizeof(t.height), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.format, bytes, sizeof(t.format), out_bytes_read, out_bytes_size);
    uint64_t byte_size = 0;
    deserialize_read_bytes_safe2((void*)&byte_size, bytes, sizeof(byte_size), out_bytes_read, out_bytes_size);
    t.data.resize(byte_size);
    deserialize_read_bytes_safe2((void*)t.data.data(), bytes, byte_size, out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<gfx::ParsedGeometryData>(const gfx::ParsedGeometryData& t, size_t& out_bytes_written,
                                                    std::byte* out_bytes, size_t out_bytes_size)
{
    serialize(std::span{ t.indices }, out_bytes_written, out_bytes, out_bytes_size);
    serialize_write_bytes_safe(out_bytes, &t.vertex_layout, out_bytes_written, out_bytes_size, sizeof(t.vertex_layout));
    serialize(std::span{ t.positions }, out_bytes_written, out_bytes, out_bytes_size);
    serialize(std::span{ t.attributes }, out_bytes_written, out_bytes, out_bytes_size);
    serialize(std::span{ t.meshlets }, out_bytes_written, out_bytes, out_bytes_size);
}

template <>
void Serializer::deserialize<gfx::ParsedGeometryData>(const std::byte* bytes, size_t& out_bytes_read,
                                                      size_t out_bytes_size, gfx::ParsedGeometryData& t)
{
    deserialize_resize_vec(t.indices, bytes, out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.vertex_layout, bytes, sizeof(t.vertex_layout), out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.positions, bytes, out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.attributes, bytes, out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.meshlets, bytes, out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<gfx::Material>(const gfx::Material& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize(std::span{ t.name.as_view() }, out_bytes_written, out_bytes, out_bytes_size);
    ENG_ASSERT(*t.mesh_pass == ~0u);
    serialize_write_bytes_safe(out_bytes, &t.mesh_pass, out_bytes_written, out_bytes_size, sizeof(t.mesh_pass));
    serialize(t.base_color_texture, out_bytes_written, out_bytes, out_bytes_size);
    serialize(t.normal_texture, out_bytes_written, out_bytes, out_bytes_size);
    serialize(t.metallic_roughness_texture, out_bytes_written, out_bytes, out_bytes_size);
}

template <>
void Serializer::deserialize<gfx::Material>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::Material& t)
{
    std::string name;
    deserialize_resize_vec(name, bytes, out_bytes_read, out_bytes_size);
    t.name = name;
    deserialize_read_bytes_safe2(&t.mesh_pass, bytes, sizeof(t.mesh_pass), out_bytes_read, out_bytes_size);
    ENG_ASSERT(*t.mesh_pass == ~0u);
    deserialize(bytes, out_bytes_read, out_bytes_size, t.base_color_texture);
    deserialize(bytes, out_bytes_read, out_bytes_size, t.normal_texture);
    deserialize(bytes, out_bytes_read, out_bytes_size, t.metallic_roughness_texture);
}

template <>
void Serializer::serialize<gfx::Mesh>(const gfx::Mesh& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize_write_bytes_safe(out_bytes, &t.geometry, out_bytes_written, out_bytes_size, sizeof(t.geometry));
    serialize_write_bytes_safe(out_bytes, &t.material, out_bytes_written, out_bytes_size, sizeof(t.material));
}

template <>
void Serializer::deserialize<gfx::Mesh>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::Mesh& t)
{
    deserialize_read_bytes_safe2(&t.geometry, bytes, sizeof(t.geometry), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.material, bytes, sizeof(t.material), out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<gfx::Meshlet>(const gfx::Meshlet& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize_write_bytes_safe(out_bytes, &t.vertex_offset, out_bytes_written, out_bytes_size, sizeof(t.vertex_offset));
    serialize_write_bytes_safe(out_bytes, &t.vertex_count, out_bytes_written, out_bytes_size, sizeof(t.vertex_count));
    serialize_write_bytes_safe(out_bytes, &t.index_offset, out_bytes_written, out_bytes_size, sizeof(t.index_offset));
    serialize_write_bytes_safe(out_bytes, &t.index_count, out_bytes_written, out_bytes_size, sizeof(t.index_count));
    serialize_write_bytes_safe(out_bytes, &t.bounding_sphere, out_bytes_written, out_bytes_size, sizeof(t.bounding_sphere));
}

template <>
void Serializer::deserialize<gfx::Meshlet>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::Meshlet& t)
{
    deserialize_read_bytes_safe2(&t.vertex_offset, bytes, sizeof(t.vertex_offset), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.vertex_count, bytes, sizeof(t.vertex_count), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.index_offset, bytes, sizeof(t.index_offset), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.index_count, bytes, sizeof(t.index_count), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.bounding_sphere, bytes, sizeof(t.bounding_sphere), out_bytes_read, out_bytes_size);
}

template <>
void Serializer::serialize<ecs::Transform>(const ecs::Transform& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    const auto mat = t.to_mat4();
    serialize_write_bytes_safe(out_bytes, &mat, out_bytes_written, out_bytes_size, sizeof(mat));
}

template <>
void Serializer::deserialize<ecs::Transform>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, ecs::Transform& t)
{
    glm::mat4 mat;
    deserialize_read_bytes_safe2(&mat, bytes, sizeof(mat), out_bytes_read, out_bytes_size);
    t.init(mat);
}

template <>
void Serializer::serialize<engb::List>(const engb::List& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize_write_bytes_safe(out_bytes, &t.custom_hash, out_bytes_written, out_bytes_size, sizeof(t.custom_hash));
    serialize_write_bytes_safe(out_bytes, &t.content_hash, out_bytes_written, out_bytes_size, sizeof(t.content_hash));
    serialize_write_bytes_safe(out_bytes, &t.asset_start, out_bytes_written, out_bytes_size, sizeof(t.asset_start));
    serialize_write_bytes_safe(out_bytes, &t.asset_size, out_bytes_written, out_bytes_size, sizeof(t.asset_size));
    serialize_write_bytes_safe(out_bytes, &t.version, out_bytes_written, out_bytes_size, sizeof(t.version));
    serialize_write_bytes_safe(out_bytes, &t.flags, out_bytes_written, out_bytes_size, sizeof(t.flags));
}

template <>
void Serializer::deserialize<engb::List>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, engb::List& t)
{
    deserialize_read_bytes_safe2(&t.custom_hash, bytes, sizeof(t.custom_hash), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.content_hash, bytes, sizeof(t.content_hash), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.asset_start, bytes, sizeof(t.asset_start), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.asset_size, bytes, sizeof(t.asset_size), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.version, bytes, sizeof(t.version), out_bytes_read, out_bytes_size);
    deserialize_read_bytes_safe2(&t.flags, bytes, sizeof(t.flags), out_bytes_read, out_bytes_size);
}

} // namespace assets
} // namespace eng