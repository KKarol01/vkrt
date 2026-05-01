#include "serialization.hpp"

#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
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

void Container::read_list_section()
{
    if(!m_file || !m_file->is_read())
    {
        ENG_WARN("Couldn't open engb file {} for read", m_file ? m_file->path.string() : "<empty path>");
        m_lists_vec.clear();
        return;
    }

    // file is empty, no list to be read
    if(m_file->size == 0) { return; }

    std::byte buf[64];
    static_assert(LIST_BYTE_SZ <= std::size(buf));
    auto read_bytes = m_file->read(buf, HEADER_BYTE_SZ, 0);
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
    for(auto i = 0u; i < num_lists; ++i)
    {
        auto& l = m_lists_vec.emplace_back();
        const auto file_read = m_file->read(buf, LIST_BYTE_SZ);
        if(file_read != LIST_BYTE_SZ)
        {
            ENG_WARN("Failed reading engb container list from file {}", m_file->path.string());
            m_lists_vec.clear();
            return;
        }
        size_t out_bytes_written = 0;
        serialization::deserialize(l, std::span{ buf }, out_bytes_written);
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

void Container::serialize()
{
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

template <> void serialize<assets::Asset>(std::span<std::byte> dst, const assets::Asset& src, size_t& out_bytes_written)
{
    if(src.geometry_data.empty())
    {
        ENG_ASSERT(false,
                   "Geometry data for asset {} must not be empty when serializing. Did you forget to provide required "
                   "keep data "
                   "flag when importing?",
                   src.path.string());

        return;
    }

    serialize(dst, src.path.string(), out_bytes_written);

    ENG_ASSERT(src.images.size() == src.image_data.size());
    serialize(dst, src.image_data, out_bytes_written);

    serialize(dst, (uint64_t)src.textures.size(), out_bytes_written);
    for(auto txt : src.textures)
    {
        // store index to image array so it can later be deserialized
        *txt.image = (uint32_t)std::distance(src.images.begin(), std::ranges::find(src.images, txt.image));
        serialize(dst, txt, out_bytes_written);
    }

    ENG_ASSERT(src.geometries.size() == src.geometry_data.size());
    serialize(dst, src.geometry_data.size(), out_bytes_written);
    for(const auto& gdf : src.geometry_data_futures)
    {
        const auto& gd = gdf.get();
        serialize(dst, gd, out_bytes_written);
    }

    serialize(dst, (uint64_t)src.materials.size(), out_bytes_written);
    for(const auto& math : src.materials)
    {
        auto mat = math.get();
        // remap image handles to store indices to t.images array, so they can be safely deserialized.
        if(mat.base_color_texture)
        {
            *mat.base_color_texture.image =
                (uint32_t)std::distance(src.images.begin(), std::ranges::find(src.images, mat.base_color_texture.image));
        }
        if(mat.normal_texture)
        {
            *mat.normal_texture.image =
                (uint32_t)std::distance(src.images.begin(), std::ranges::find(src.images, mat.normal_texture.image));
        }
        if(mat.metallic_roughness_texture)
        {
            *mat.metallic_roughness_texture.image =
                (uint32_t)std::distance(src.images.begin(), std::ranges::find(src.images, mat.metallic_roughness_texture.image));
        }
        mat.mesh_pass = {}; // ignoring meshpass, as load_material uses default one
        serialize(dst, mat, out_bytes_written);
    }

    serialize(dst, (uint64_t)src.meshes.size(), out_bytes_written);
    for(const auto& meshh : src.meshes)
    {
        auto mesh = meshh.get();
        *mesh.geometry = (uint32_t)std::distance(src.geometries.begin(), std::ranges::find(src.geometries, mesh.geometry));
        if(mesh.material)
        {
            *mesh.material = (uint32_t)std::distance(src.materials.begin(), std::ranges::find(src.materials, mesh.material));
        }
        serialize(dst, mesh, out_bytes_written);
    }

    serialize(dst, src.nodes, out_bytes_written);
    serialize(dst, src.transforms, out_bytes_written);
    serialize(dst, src.root_nodes, out_bytes_written);
}

template <>
void deserialize<assets::Asset>(assets::Asset& dst, std::span<const std::byte> src, size_t& out_bytes_written)
{
    std::string pathstr;
    deserialize(pathstr, src, out_bytes_written);
    dst.path = pathstr;
    ENG_ASSERT(!dst.path.empty())

    uint64_t image_count;
    deserialize(image_count, src, out_bytes_written);
    dst.images.resize(image_count);
    gfx::ParsedImageData imgd{};
    for(auto i = 0u; i < image_count; ++i)
    {
        // don't use deserialize() here, because it would double the image data for no reason, we can read from the stream
        uint64_t pixel_data_size = 0;
        deserialize(imgd.width, src, out_bytes_written);
        deserialize(imgd.height, src, out_bytes_written);
        deserialize(imgd.format, src, out_bytes_written);
        deserialize(pixel_data_size, src, out_bytes_written);
        dst.images[i] = get_engine().renderer->make_image("EMPTY IMAGE NAME",
                                                          gfx::Image::init(imgd.width, imgd.height, 0, imgd.format,
                                                                           gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                               gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                           0, 1, gfx::ImageLayout::READ_ONLY));
        if(!dst.images[i])
        {
            ENG_WARN("Failed to create image {}", "EMPTY IMAGE NAME");
            continue;
        }
        else
        {
            gfx::get_renderer().staging->copy(dst.images[i].get(), src.data() + out_bytes_written, 0, 0, true,
                                              gfx::DiscardContents::YES);
            ENG_TODO("TODO: Process mips");
        }
        out_bytes_written += pixel_data_size;
    }

    uint64_t texture_count;
    deserialize(texture_count, src, out_bytes_written);
    dst.textures.resize(texture_count);
    for(auto& txt : dst.textures)
    {
        deserialize(txt, src, out_bytes_written);
        txt.image = dst.images[*txt.image]; // when serializing, handles are made into indices into this array
    }

    uint64_t geom_count;
    deserialize(geom_count, src, out_bytes_written);
    dst.geometries.resize(geom_count);
    gfx::ParsedGeometryData geom;
    for(uint64_t i = 0; i < geom_count; ++i)
    {
        deserialize(geom, src, out_bytes_written);
        dst.geometries[i] =
            gfx::get_renderer().make_geometry(gfx::GeometryDescriptor{ .flags = {},
                                                                       .vertex_layout = geom.vertex_layout,
                                                                       .index_format = gfx::IndexFormat::U16,
                                                                       .vertices = geom.positions,
                                                                       .attributes = geom.attributes,
                                                                       .indices = std::as_bytes(std::span{ geom.indices }),
                                                                       .meshlets = geom.meshlets });
    }

    uint64_t material_count;
    deserialize(material_count, src, out_bytes_written);
    dst.materials.resize(material_count);
    gfx::Material mat;
    for(uint64_t i = 0; i < material_count; ++i)
    {
        deserialize(mat, src, out_bytes_written);
        if(mat.base_color_texture) { mat.base_color_texture = dst.textures[*mat.base_color_texture.image]; }
        if(mat.normal_texture) { mat.normal_texture = dst.textures[*mat.normal_texture.image]; }
        if(mat.metallic_roughness_texture)
        {
            mat.metallic_roughness_texture = dst.textures[*mat.metallic_roughness_texture.image];
        }
        dst.materials[i] = gfx::get_renderer().make_material(mat);
    }

    uint64_t mesh_count = 0;
    deserialize(mesh_count, src, out_bytes_written);
    dst.meshes.resize(mesh_count);
    gfx::Mesh mesh;
    for(uint64_t i = 0; i < mesh_count; ++i)
    {
        deserialize(mesh, src, out_bytes_written);
        if(mesh.material) { mesh.material = dst.materials[*mesh.material]; }
        if(mesh.geometry) { mesh.geometry = dst.geometries[*mesh.geometry]; }
        dst.meshes[i] = gfx::get_renderer().make_mesh(gfx::MeshDescriptor{ mesh.geometry, mesh.material });
    }

    deserialize(dst.nodes, src, out_bytes_written);
    deserialize(dst.transforms, src, out_bytes_written);
    deserialize(dst.root_nodes, src, out_bytes_written);
}

template <>
void serialize<ecs::Transform>(std::span<std::byte> dst, const ecs::Transform& src, size_t& out_bytes_written)
{
    const auto mat = src.to_mat4();
    safe_write(dst.data(), &mat, out_bytes_written, dst.size_bytes(), sizeof(mat));
}

template <>
void deserialize<ecs::Transform>(ecs::Transform& dst, std::span<const std::byte> src, size_t& out_bytes_written)
{
    glm::mat4 mat;
    safe_read(&mat, src.data(), out_bytes_written, sizeof(mat), src.size_bytes());
    dst.init(mat);
}

#if 0
template <>
void serialize<assets::Asset>(const assets::Asset& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
   
}

template <>
void deserialize<assets::Asset>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, assets::Asset& t)
{

}

template <>
void serialize<assets::Node>(const assets::Node& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize(std::string_view{ t.name }, out_bytes_written, out_bytes, out_bytes_size);
    write_safe(out_bytes, &t.meshes, out_bytes_written, out_bytes_size, sizeof(t.meshes));
    write_safe(out_bytes, &t.transform, out_bytes_written, out_bytes_size, sizeof(t.transform));
    write_safe(out_bytes, &t.parent, out_bytes_written, out_bytes_size, sizeof(t.parent));
    write_safe(out_bytes, &t.children, out_bytes_written, out_bytes_size, sizeof(t.children));
}

template <>
void deserialize<assets::Node>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, assets::Node& t)
{
    deserialize(bytes, out_bytes_read, out_bytes_size, t.name);
    read_safe(&t.meshes, bytes, out_bytes_read, sizeof(t.meshes), out_bytes_size);
    read_safe(&t.transform, bytes, out_bytes_read, sizeof(t.transform), out_bytes_size);
    read_safe(&t.parent, bytes, out_bytes_read, sizeof(t.parent), out_bytes_size);
    read_safe(&t.children, bytes, out_bytes_read, sizeof(t.children), out_bytes_size);
}

template <>
void serialize<gfx::ImageView>(const gfx::ImageView& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    write_safe(out_bytes, &t.image, out_bytes_written, out_bytes_size, sizeof(t.image));
    write_safe(out_bytes, &t.type, out_bytes_written, out_bytes_size, sizeof(t.type));
    write_safe(out_bytes, &t.format, out_bytes_written, out_bytes_size, sizeof(t.format));
    write_safe(out_bytes, &t.src_subresource, out_bytes_written, out_bytes_size, sizeof(t.src_subresource));
    write_safe(out_bytes, &t.dst_subresource, out_bytes_written, out_bytes_size, sizeof(t.dst_subresource));
}

template <>
void deserialize<gfx::ImageView>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::ImageView& t)
{
    read_safe(&t.image, bytes, out_bytes_read, sizeof(t.image), out_bytes_size);
    read_safe(&t.type, bytes, out_bytes_read, sizeof(t.type), out_bytes_size);
    read_safe(&t.format, bytes, out_bytes_read, sizeof(t.format), out_bytes_size);
    read_safe(&t.src_subresource, bytes, out_bytes_read, sizeof(t.src_subresource), out_bytes_size);
    read_safe(&t.dst_subresource, bytes, out_bytes_read, sizeof(t.dst_subresource), out_bytes_size);
}

template <>
void serialize<gfx::ParsedImageData>(const gfx::ParsedImageData& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    write_safe(out_bytes, &t.width, out_bytes_written, out_bytes_size, sizeof(t.width));
    write_safe(out_bytes, &t.height, out_bytes_written, out_bytes_size, sizeof(t.height));
    write_safe(out_bytes, &t.format, out_bytes_written, out_bytes_size, sizeof(t.format));
    const uint64_t byte_size = t.data.size();
    write_safe(out_bytes, &byte_size, out_bytes_written, out_bytes_size, sizeof(byte_size));
    write_safe(out_bytes, t.data.data(), out_bytes_written, out_bytes_size, byte_size);
}

template <>
void deserialize<gfx::ParsedImageData>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::ParsedImageData& t)
{
    read_safe(&t.width, bytes, out_bytes_read, sizeof(t.width), out_bytes_size);
    read_safe(&t.height, bytes, out_bytes_read, sizeof(t.height), out_bytes_size);
    read_safe(&t.format, bytes, out_bytes_read, sizeof(t.format), out_bytes_size);
    uint64_t byte_size = 0;
    read_safe((void*)&byte_size, bytes, out_bytes_read, sizeof(byte_size), out_bytes_size);
    t.data.resize(byte_size);
    read_safe((void*)t.data.data(), bytes, byte_size, out_bytes_read, out_bytes_size);
}

template <>
void serialize<gfx::ParsedGeometryData>(const gfx::ParsedGeometryData& t, size_t& out_bytes_written,
                                        std::byte* out_bytes, size_t out_bytes_size)
{
    serialize(std::span{ t.indices }, out_bytes_written, out_bytes, out_bytes_size);
    write_safe(out_bytes, &t.vertex_layout, out_bytes_written, out_bytes_size, sizeof(t.vertex_layout));
    serialize(std::span{ t.positions }, out_bytes_written, out_bytes, out_bytes_size);
    serialize(std::span{ t.attributes }, out_bytes_written, out_bytes, out_bytes_size);
    serialize(std::span{ t.meshlets }, out_bytes_written, out_bytes, out_bytes_size);
}

template <>
void deserialize<gfx::ParsedGeometryData>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size,
                                          gfx::ParsedGeometryData& t)
{
    deserialize_resize_vec(t.indices, bytes, out_bytes_read, out_bytes_size);
    read_safe(&t.vertex_layout, bytes, out_bytes_read, sizeof(t.vertex_layout), out_bytes_size);
    deserialize_resize_vec(t.positions, bytes, out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.attributes, bytes, out_bytes_read, out_bytes_size);
    deserialize_resize_vec(t.meshlets, bytes, out_bytes_read, out_bytes_size);
}

template <>
void serialize<gfx::Material>(const gfx::Material& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    serialize(std::span{ t.name.as_view() }, out_bytes_written, out_bytes, out_bytes_size);
    ENG_ASSERT(*t.mesh_pass == ~0u);
    write_safe(out_bytes, &t.mesh_pass, out_bytes_written, out_bytes_size, sizeof(t.mesh_pass));
    serialize(t.base_color_texture, out_bytes_written, out_bytes, out_bytes_size);
    serialize(t.normal_texture, out_bytes_written, out_bytes, out_bytes_size);
    serialize(t.metallic_roughness_texture, out_bytes_written, out_bytes, out_bytes_size);
}

template <>
void deserialize<gfx::Material>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::Material& t)
{
    std::string name;
    deserialize_resize_vec(name, bytes, out_bytes_read, out_bytes_size);
    t.name = name;
    read_safe(&t.mesh_pass, bytes, out_bytes_read, sizeof(t.mesh_pass), out_bytes_size);
    ENG_ASSERT(*t.mesh_pass == ~0u);
    deserialize(bytes, out_bytes_read, out_bytes_size, t.base_color_texture);
    deserialize(bytes, out_bytes_read, out_bytes_size, t.normal_texture);
    deserialize(bytes, out_bytes_read, out_bytes_size, t.metallic_roughness_texture);
}

template <>
void serialize<gfx::Mesh>(const gfx::Mesh& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    write_safe(out_bytes, &t.geometry, out_bytes_written, out_bytes_size, sizeof(t.geometry));
    write_safe(out_bytes, &t.material, out_bytes_written, out_bytes_size, sizeof(t.material));
}

template <>
void deserialize<gfx::Mesh>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::Mesh& t)
{
    read_safe(&t.geometry, bytes, out_bytes_read, sizeof(t.geometry), out_bytes_size);
    read_safe(&t.material, bytes, out_bytes_read, sizeof(t.material), out_bytes_size);
}

template <>
void serialize<gfx::Meshlet>(const gfx::Meshlet& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    write_safe(out_bytes, &t.vertex_offset, out_bytes_written, out_bytes_size, sizeof(t.vertex_offset));
    write_safe(out_bytes, &t.vertex_count, out_bytes_written, out_bytes_size, sizeof(t.vertex_count));
    write_safe(out_bytes, &t.index_offset, out_bytes_written, out_bytes_size, sizeof(t.index_offset));
    write_safe(out_bytes, &t.index_count, out_bytes_written, out_bytes_size, sizeof(t.index_count));
    write_safe(out_bytes, &t.bounding_sphere, out_bytes_written, out_bytes_size, sizeof(t.bounding_sphere));
}

template <>
void deserialize<gfx::Meshlet>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, gfx::Meshlet& t)
{
    read_safe(&t.vertex_offset, bytes, out_bytes_read, sizeof(t.vertex_offset), out_bytes_size);
    read_safe(&t.vertex_count, bytes, out_bytes_read, sizeof(t.vertex_count), out_bytes_size);
    read_safe(&t.index_offset, bytes, out_bytes_read, sizeof(t.index_offset), out_bytes_size);
    read_safe(&t.index_count, bytes, out_bytes_read, sizeof(t.index_count), out_bytes_size);
    read_safe(&t.bounding_sphere, bytes, out_bytes_read, sizeof(t.bounding_sphere), out_bytes_size);
}


template <>
void serialize<engb::List>(const engb::List& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    write_safe(out_bytes, &t.custom_hash, out_bytes_written, out_bytes_size, sizeof(t.custom_hash));
    write_safe(out_bytes, &t.content_hash, out_bytes_written, out_bytes_size, sizeof(t.content_hash));
    write_safe(out_bytes, &t.asset_start, out_bytes_written, out_bytes_size, sizeof(t.asset_start));
    write_safe(out_bytes, &t.asset_size, out_bytes_written, out_bytes_size, sizeof(t.asset_size));
    write_safe(out_bytes, &t.version, out_bytes_written, out_bytes_size, sizeof(t.version));
    write_safe(out_bytes, &t.flags, out_bytes_written, out_bytes_size, sizeof(t.flags));
}

template <>
void deserialize<engb::List>(const std::byte* bytes, size_t& out_bytes_read, size_t out_bytes_size, engb::List& t)
{
    read_safe(&t.custom_hash, bytes, out_bytes_read, sizeof(t.custom_hash), out_bytes_size);
    read_safe(&t.content_hash, bytes, out_bytes_read, sizeof(t.content_hash), out_bytes_size);
    read_safe(&t.asset_start, bytes, out_bytes_read, sizeof(t.asset_start), out_bytes_size);
    read_safe(&t.asset_size, bytes, out_bytes_read, sizeof(t.asset_size), out_bytes_size);
    read_safe(&t.version, bytes, out_bytes_read, sizeof(t.version), out_bytes_size);
    read_safe(&t.flags, bytes, out_bytes_read, sizeof(t.flags), out_bytes_size);
}
#endif

} // namespace serialization
} // namespace eng