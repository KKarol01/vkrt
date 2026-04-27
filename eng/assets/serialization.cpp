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

// Tries to write bytes. Always increments dst_offset so that it can be used to calculate how many bytes a thing would take, preallocate storage, and be run again.
static void serialize_write_bytes_safe(void* dst, const void* src, size_t& dst_offset, size_t dst_size, size_t src_size)
{
    if(dst && src && dst_offset + src_size <= dst_size) { std::memcpy((std::byte*)dst + dst_offset, src, src_size); }
    dst_offset += src_size;
}

// Tries to read bytes. Increments src_offset only on success, so number of deserialized bytes can be compared against reference.
static void deserialize_read_bytes_safe(void* dst, const void* src, size_t& src_offset, size_t dst_size, size_t src_size)
{
    if(src_offset + src_size <= dst_size)
    {
        std::memcpy(dst, (const std::byte*)src + src_offset, src_size);
        src_offset += src_size;
    }
};

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
    m_file->close();
}

void Container::read_list_section()
{
    if(!m_file || !m_file->is_open())
    {
        ENG_WARN("Couldn't open engb file {} for read", m_file ? m_file->path.string() : "<empty path>");
        m_lists.clear();
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
    const size_t lists_bytes = num_lists * LIST_BYTE_SZ;
    constexpr size_t lists_per_buf = std::size(buf) / LIST_BYTE_SZ;
    m_lists.reserve(num_lists);
    while(num_lists > 0)
    {
        const auto lists_left = std::min((size_t)num_lists, lists_per_buf);
        const auto file_read = m_file->read(buf, lists_left * LIST_BYTE_SZ);
        if(file_read != lists_left * LIST_BYTE_SZ)
        {
            ENG_WARN("Failed to read engb container {} list section", m_file->path.string());
            return;
        }

        size_t bytes_read{};
        for(auto i = 0u; i < lists_per_buf; ++i)
        {
            auto& list = m_lists.emplace_back();
            Serializer::deserialize(buf, bytes_read, lists_bytes, list);
        }
    }
}

void Container::add_asset(uint8_t version, uint64_t custom_hash, Flags<ListFlags> flags,
                          std::span<const std::byte> asset, AssetMetadata metadata)
{
    m_lists.emplace_back(custom_hash, ENG_HASH(asset), m_asset_bytes.size(), asset.size(), version, flags);

    std::byte buf[64];
    size_t metadata_bytes = 0;
    if(flags & ListFlags::CONTENT_COMPRESSED_BIT)
    {
        ENG_ASSERT(metadata.uncompressed_size > 0);
        memcpy(buf, &metadata.uncompressed_size, sizeof(metadata.uncompressed_size));
    }

    ENG_ASSERT(metadata_bytes <= std::size(buf));
    m_asset_bytes.insert(m_asset_bytes.end(), buf, buf + metadata_bytes);
    m_asset_bytes.insert(m_asset_bytes.end(), asset.begin(), asset.end());
}

void Container::append_asset_bytes(std::span<const std::byte> bytes, bool finished)
{
    m_asset_bytes.insert(m_asset_bytes.end(), bytes.begin(), bytes.end());
    m_lists.back().asset_size += bytes.size();
    if(finished)
    {
        m_lists.back().content_hash =
            ENG_HASH(ENG_HASH_AS_SPAN(m_asset_bytes.begin() + m_lists.back().asset_start, m_lists.back().asset_size));
    }
}

void Container::serialize(size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    static constexpr const char* MAGIC = "engb";
    static constexpr uint8_t VERSION = 0;

    serialize_write_bytes_safe(out_bytes, MAGIC, out_bytes_written, out_bytes_size, 4);
    serialize_write_bytes_safe(out_bytes, &VERSION, out_bytes_written, out_bytes_size, 1);

    const uint32_t item_count = (uint32_t)m_lists.size();
    serialize_write_bytes_safe(out_bytes, &item_count, out_bytes_written, out_bytes_size, 4);
    for(auto& l : m_lists)
    {
        l.asset_start = HEADER_BYTE_SZ + (m_lists.size() * LIST_BYTE_SZ) + l.asset_start;
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
    m_file->open();
    const auto total_size_bytes = HEADER_BYTE_SZ + (m_lists.size() * LIST_BYTE_SZ) + m_asset_bytes.size();
    std::vector<std::byte> data(total_size_bytes);
    serialize(out_bytes_written, data.data(), total_size_bytes);
    ENG_ASSERT(total_size_bytes == out_bytes_written);
    const auto file_bytes_writen = m_file->write(data.data(), out_bytes_written);
    ENG_ASSERT(file_bytes_writen == total_size_bytes);
    m_file->close();
    return out_bytes_written;
}

} // namespace v0
} // namespace engb

template <>
void Serializer::serialize<assets::Asset>(const assets::Asset& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    out_bytes_written = 0;

    if(t.geometry_data.empty())
    {
        ENG_ASSERT(false,
                   "Geometry data must not be empty when serializing. Did you forget to provide required keep data "
                   "flag when importing?");

        return;
    }

    const auto pathstr = t.path.string();
    const uint64_t path_length = pathstr.size();
    serialize_write_bytes_safe(out_bytes, &path_length, out_bytes_written, out_bytes_size, sizeof(path_length));
    serialize_write_bytes_safe(out_bytes, pathstr.c_str(), out_bytes_written, out_bytes_size, pathstr.size());

    ENG_ASSERT(t.images.size() == t.image_data.size());
    const uint64_t image_count = t.images.size();
    serialize_write_bytes_safe(out_bytes, &image_count, out_bytes_written, out_bytes_size, sizeof(image_count));
    for(const auto& imgd : t.image_data)
    {
        serialize_write_bytes_safe(out_bytes, &imgd.width, out_bytes_written, out_bytes_size, sizeof(imgd.width));
        serialize_write_bytes_safe(out_bytes, &imgd.height, out_bytes_written, out_bytes_size, sizeof(imgd.height));
        serialize_write_bytes_safe(out_bytes, &imgd.format, out_bytes_written, out_bytes_size, sizeof(imgd.format));
        const uint64_t byte_size = imgd.data.size();
        serialize_write_bytes_safe(out_bytes, &byte_size, out_bytes_written, out_bytes_size, sizeof(byte_size));
        serialize_write_bytes_safe(out_bytes, imgd.data.data(), out_bytes_written, out_bytes_size, byte_size);
    }

    // transform imageviews handles to store indices into asset's image array, and not actual renderer-generated image handles
    auto textures =
        t.textures | std::views::transform([&t](const gfx::ImageView& v) {
            auto view = v;
            view.image = Handle<gfx::Image>{ (uint32_t)std::distance(t.images.begin(), std::ranges::find(t.images, v.image)) };
            return view;
        });
    const uint64_t texture_count = textures.size();
    serialize_write_bytes_safe(out_bytes, &texture_count, out_bytes_written, out_bytes_size, sizeof(texture_count));
    for(const auto& txt : textures)
    {
        serialize(txt, out_bytes_written, out_bytes, out_bytes_size);
    }

    ENG_ASSERT(t.geometries.size() == t.geometry_data.size());
    const uint64_t geom_count = t.geometries.size();
    serialize_write_bytes_safe(out_bytes, &geom_count, out_bytes_written, out_bytes_size, sizeof(geom_count));
    for(const auto& future : t.geometry_data_futures)
    {
        const auto& geom = future.get();
        const uint64_t index_count = geom.indices.size();
        serialize_write_bytes_safe(out_bytes, &index_count, out_bytes_written, out_bytes_size, sizeof(index_count));
        serialize_write_bytes_safe(out_bytes, geom.indices.data(), out_bytes_written, out_bytes_size,
                                   geom.indices.size() * sizeof(uint16_t));

        serialize_write_bytes_safe(out_bytes, &geom.vertex_layout, out_bytes_written, out_bytes_size, sizeof(geom.vertex_layout));
        const uint64_t position_count = geom.positions.size();
        serialize_write_bytes_safe(out_bytes, &position_count, out_bytes_written, out_bytes_size, sizeof(position_count));
        serialize_write_bytes_safe(out_bytes, geom.positions.data(), out_bytes_written, out_bytes_size,
                                   geom.positions.size() * sizeof(float));
        const uint64_t attribute_count = geom.attributes.size();
        serialize_write_bytes_safe(out_bytes, &attribute_count, out_bytes_written, out_bytes_size, sizeof(attribute_count));
        serialize_write_bytes_safe(out_bytes, geom.attributes.data(), out_bytes_written, out_bytes_size,
                                   geom.attributes.size() * sizeof(float));

        const uint64_t meshlet_count = geom.meshlet.size();
        serialize_write_bytes_safe(out_bytes, &meshlet_count, out_bytes_written, out_bytes_size, sizeof(meshlet_count));
        serialize_write_bytes_safe(out_bytes, geom.meshlet.data(), out_bytes_written, out_bytes_size,
                                   geom.meshlet.size() * sizeof(geom.meshlet[0]));
    }

    const uint64_t material_count = t.materials.size();
    serialize_write_bytes_safe(out_bytes, &material_count, out_bytes_written, out_bytes_size, sizeof(material_count));
    for(const auto& mat : t.materials)
    {
        const auto name_view = mat->name.as_view();
        const uint64_t name_len = name_view.size();
        serialize_write_bytes_safe(out_bytes, &name_len, out_bytes_written, out_bytes_size, sizeof(name_len));
        serialize_write_bytes_safe(out_bytes, name_view.data(), out_bytes_written, out_bytes_size, name_len);
        const uint64_t base_color_idx =
            mat->base_color_texture
                ? std::distance(t.images.begin(), std::ranges::find(t.images, mat->base_color_texture.image))
                : ~0ull;
        const uint64_t normal_idx =
            mat->normal_texture ? std::distance(t.images.begin(), std::ranges::find(t.images, mat->normal_texture.image)) : ~0ull;
        const uint64_t metallic_rough_idx =
            mat->metallic_roughness_texture
                ? std::distance(t.images.begin(), std::ranges::find(t.images, mat->metallic_roughness_texture.image))
                : ~0ull;
        const uint32_t mesh_pass = ~0u; // ignoring meshpass, as load_material uses default one

        serialize_write_bytes_safe(out_bytes, &mesh_pass, out_bytes_written, out_bytes_size, sizeof(mesh_pass));

        serialize_write_bytes_safe(out_bytes, &base_color_idx, out_bytes_written, out_bytes_size, sizeof(base_color_idx));
        serialize_write_bytes_safe(out_bytes, &normal_idx, out_bytes_written, out_bytes_size, sizeof(normal_idx));
        serialize_write_bytes_safe(out_bytes, &metallic_rough_idx, out_bytes_written, out_bytes_size, sizeof(metallic_rough_idx));
    }

    const uint64_t mesh_count = t.meshes.size();
    serialize_write_bytes_safe(out_bytes, &mesh_count, out_bytes_written, out_bytes_size, sizeof(mesh_count));
    for(const auto& mesh : t.meshes)
    {
        const uint64_t geomidx = std::distance(t.geometries.begin(), std::ranges::find(t.geometries, mesh->geometry));
        const uint64_t matidx = std::distance(t.materials.begin(), std::ranges::find(t.materials, mesh->material));
        serialize_write_bytes_safe(out_bytes, &geomidx, out_bytes_written, out_bytes_size, sizeof(geomidx));
        serialize_write_bytes_safe(out_bytes, &matidx, out_bytes_written, out_bytes_size, sizeof(matidx));
    }

    const uint64_t node_count = t.nodes.size();
    serialize_write_bytes_safe(out_bytes, &node_count, out_bytes_written, out_bytes_size, sizeof(node_count));
    for(auto i = 0u; i < node_count; ++i)
    {
        const auto& node = t.nodes[i];
        const auto name_len = node.name.size();
        serialize_write_bytes_safe(out_bytes, &name_len, out_bytes_written, out_bytes_size, sizeof(name_len));
        serialize_write_bytes_safe(out_bytes, node.name.c_str(), out_bytes_written, out_bytes_size, name_len);
        serialize_write_bytes_safe(out_bytes, &node.meshes, out_bytes_written, out_bytes_size, sizeof(node.meshes));
        serialize_write_bytes_safe(out_bytes, &node.transform, out_bytes_written, out_bytes_size, sizeof(node.transform));
        serialize_write_bytes_safe(out_bytes, &node.parent, out_bytes_written, out_bytes_size, sizeof(node.parent));
        serialize_write_bytes_safe(out_bytes, &node.children, out_bytes_written, out_bytes_size, sizeof(node.children));
    }

    const uint64_t transform_count = t.transforms.size();
    serialize_write_bytes_safe(out_bytes, &transform_count, out_bytes_written, out_bytes_size, sizeof(transform_count));
    for(const auto& t : t.transforms)
    {
        serialize(t, out_bytes_written, out_bytes, out_bytes_size);
    }

    const uint64_t root_node_count = t.root_nodes.size();
    serialize_write_bytes_safe(out_bytes, &root_node_count, out_bytes_written, out_bytes_size, sizeof(root_node_count));
    serialize_write_bytes_safe(out_bytes, t.root_nodes.data(), out_bytes_written, out_bytes_size,
                               root_node_count * sizeof(t.root_nodes[0]));
}

template <>
void Serializer::deserialize<assets::Asset>(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, assets::Asset& t)
{
    uint64_t path_length = 0;
    deserialize_read_bytes_safe(&path_length, bytes, out_bytes_read, bytes_size, sizeof(path_length));
    std::string pathstr(path_length, '\0');
    deserialize_read_bytes_safe(pathstr.data(), bytes, out_bytes_read, bytes_size, path_length);
    t.path = pathstr;

    uint64_t image_count = 0;
    deserialize_read_bytes_safe(&image_count, bytes, out_bytes_read, bytes_size, sizeof(image_count));
    gfx::ParsedImageData imgd{};
    t.images.resize(image_count);
    for(auto i = 0u; i < image_count; ++i)
    {
        deserialize_read_bytes_safe(&imgd.width, bytes, out_bytes_read, bytes_size, sizeof(imgd.width));
        deserialize_read_bytes_safe(&imgd.height, bytes, out_bytes_read, bytes_size, sizeof(imgd.height));
        deserialize_read_bytes_safe(&imgd.format, bytes, out_bytes_read, bytes_size, sizeof(imgd.format));
        uint64_t byte_size = 0;
        deserialize_read_bytes_safe(&byte_size, bytes, out_bytes_read, bytes_size, sizeof(byte_size));
        t.images[i] = get_engine().renderer->make_image("EMPTY IMAGE NAME",
                                                        gfx::Image::init(imgd.width, imgd.height, 0, imgd.format,
                                                                         gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                             gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                         0, 1, gfx::ImageLayout::READ_ONLY));
        const auto imgdata_start = out_bytes_read;
        out_bytes_read += byte_size;
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

    uint64_t texture_count = 0;
    deserialize_read_bytes_safe(&texture_count, bytes, out_bytes_read, bytes_size, sizeof(texture_count));
    t.textures.resize(texture_count);
    for(auto& txt : t.textures)
    {
        deserialize(bytes, out_bytes_read, bytes_size, txt);
        txt.image = t.images[*txt.image]; // when serializing, handles are made into indices into this array
    }

    uint64_t geom_count = 0;
    deserialize_read_bytes_safe(&geom_count, bytes, out_bytes_read, bytes_size, sizeof(geom_count));
    t.geometries.resize(geom_count);
    for(uint64_t i = 0; i < geom_count; ++i)
    {
        gfx::ParsedGeometryData geom;

        uint64_t index_count = 0;
        deserialize_read_bytes_safe(&index_count, bytes, out_bytes_read, bytes_size, sizeof(index_count));
        geom.indices.resize(index_count);
        deserialize_read_bytes_safe(geom.indices.data(), bytes, out_bytes_read, bytes_size, index_count * sizeof(uint16_t));

        deserialize_read_bytes_safe(&geom.vertex_layout, bytes, out_bytes_read, bytes_size, sizeof(geom.vertex_layout));
        uint64_t position_count = 0;
        deserialize_read_bytes_safe(&position_count, bytes, out_bytes_read, bytes_size, sizeof(position_count));
        geom.positions.resize(position_count);
        deserialize_read_bytes_safe(geom.positions.data(), bytes, out_bytes_read, bytes_size, position_count * sizeof(float));
        uint64_t attribute_count = 0;
        deserialize_read_bytes_safe(&attribute_count, bytes, out_bytes_read, bytes_size, sizeof(attribute_count));
        geom.attributes.resize(attribute_count);
        deserialize_read_bytes_safe(geom.attributes.data(), bytes, out_bytes_read, bytes_size, attribute_count * sizeof(float));

        uint64_t meshlet_count = 0;
        deserialize_read_bytes_safe(&meshlet_count, bytes, out_bytes_read, bytes_size, sizeof(meshlet_count));
        geom.meshlet.resize(meshlet_count);
        deserialize_read_bytes_safe(geom.meshlet.data(), bytes, out_bytes_read, bytes_size,
                                    meshlet_count * sizeof(geom.meshlet[0]));

        t.geometries[i] =
            gfx::get_renderer().make_geometry(gfx::GeometryDescriptor{ .flags = {},
                                                                       .vertex_layout = geom.vertex_layout,
                                                                       .index_format = gfx::IndexFormat::U16,
                                                                       .vertices = geom.positions,
                                                                       .attributes = geom.attributes,
                                                                       .indices = std::as_bytes(std::span{ geom.indices }),
                                                                       .meshlets = geom.meshlet });
    }

    uint64_t material_count = 0;
    deserialize_read_bytes_safe(&material_count, bytes, out_bytes_read, bytes_size, sizeof(material_count));
    t.materials.resize(material_count);
    for(uint64_t i = 0; i < material_count; ++i)
    {
        uint64_t name_len = 0;
        deserialize_read_bytes_safe(&name_len, bytes, out_bytes_read, bytes_size, sizeof(name_len));
        std::string_view name((const char*)bytes + out_bytes_read, name_len);
        out_bytes_read += name_len;
        uint32_t mesh_pass;
        deserialize_read_bytes_safe(&mesh_pass, bytes, out_bytes_read, bytes_size, sizeof(mesh_pass));
        uint64_t bidx, nidx, mridx;
        deserialize_read_bytes_safe(&bidx, bytes, out_bytes_read, bytes_size, sizeof(bidx));
        deserialize_read_bytes_safe(&nidx, bytes, out_bytes_read, bytes_size, sizeof(nidx));
        deserialize_read_bytes_safe(&mridx, bytes, out_bytes_read, bytes_size, sizeof(mridx));
        auto mat = gfx::Material::init(name);
        // if(bidx != ~0u) { mat.base_color_texture.init(t.images[bidx]); }
        // if(nidx != ~0u) { mat.normal_texture.init(t.images[nidx]); }
        // if(mridx != ~0u) { mat.metallic_roughness_texture.init(t.images[mridx]); }
        t.materials[i] = gfx::get_renderer().make_material(mat);
    }

    uint64_t mesh_count = 0;
    deserialize_read_bytes_safe(&mesh_count, bytes, out_bytes_read, bytes_size, sizeof(mesh_count));
    t.meshes.resize(mesh_count);
    for(uint64_t i = 0; i < mesh_count; ++i)
    {
        uint64_t geomidx, matidx;
        deserialize_read_bytes_safe(&geomidx, bytes, out_bytes_read, bytes_size, sizeof(geomidx));
        deserialize_read_bytes_safe(&matidx, bytes, out_bytes_read, bytes_size, sizeof(matidx));
        t.meshes[i] = gfx::get_renderer().make_mesh(gfx::MeshDescriptor{ t.geometries[geomidx], t.materials[matidx] });
    }

    uint64_t node_count = 0;
    deserialize_read_bytes_safe(&node_count, bytes, out_bytes_read, bytes_size, sizeof(node_count));
    t.nodes.resize(node_count);
    for(auto& node : t.nodes)
    {
        uint64_t name_len = 0;
        deserialize_read_bytes_safe(&name_len, bytes, out_bytes_read, bytes_size, sizeof(name_len));
        node.name.resize(name_len);
        deserialize_read_bytes_safe(node.name.data(), bytes, out_bytes_read, bytes_size, name_len);
        deserialize_read_bytes_safe(&node.meshes, bytes, out_bytes_read, bytes_size, sizeof(node.meshes));
        deserialize_read_bytes_safe(&node.transform, bytes, out_bytes_read, bytes_size, sizeof(node.transform));
        deserialize_read_bytes_safe(&node.parent, bytes, out_bytes_read, bytes_size, sizeof(node.parent));
        deserialize_read_bytes_safe(&node.children, bytes, out_bytes_read, bytes_size, sizeof(node.children));
    }

    uint64_t transform_count = 0;
    deserialize_read_bytes_safe(&transform_count, bytes, out_bytes_read, bytes_size, sizeof(transform_count));
    t.transforms.resize(transform_count);
    for(auto& trans : t.transforms)
    {
        deserialize(bytes, out_bytes_read, bytes_size, trans);
    }

    uint64_t root_node_count = 0;
    deserialize_read_bytes_safe(&root_node_count, bytes, out_bytes_read, bytes_size, sizeof(root_node_count));
    t.root_nodes.resize(root_node_count);
    deserialize_read_bytes_safe(t.root_nodes.data(), bytes, out_bytes_read, bytes_size, root_node_count * sizeof(t.root_nodes[0]));
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
void Serializer::deserialize<gfx::ImageView>(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, gfx::ImageView& t)
{
    deserialize_read_bytes_safe(&t.image, bytes, out_bytes_read, bytes_size, sizeof(t.image));
    deserialize_read_bytes_safe(&t.type, bytes, out_bytes_read, bytes_size, sizeof(t.type));
    deserialize_read_bytes_safe(&t.format, bytes, out_bytes_read, bytes_size, sizeof(t.format));
    deserialize_read_bytes_safe(&t.src_subresource, bytes, out_bytes_read, bytes_size, sizeof(t.src_subresource));
    deserialize_read_bytes_safe(&t.dst_subresource, bytes, out_bytes_read, bytes_size, sizeof(t.dst_subresource));
}

template <>
void Serializer::serialize<ecs::Transform>(const ecs::Transform& t, size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    const auto mat = t.to_mat4();
    serialize_write_bytes_safe(out_bytes, &mat, out_bytes_written, out_bytes_size, sizeof(mat));
}

template <>
void Serializer::deserialize<ecs::Transform>(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, ecs::Transform& t)
{
    glm::mat4 mat;
    deserialize_read_bytes_safe(&mat, bytes, out_bytes_read, bytes_size, sizeof(mat));
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
void Serializer::deserialize<engb::List>(const std::byte* bytes, size_t& out_bytes_read, size_t bytes_size, engb::List& t)
{
    deserialize_read_bytes_safe(&t.custom_hash, bytes, out_bytes_read, bytes_size, sizeof(t.custom_hash));
    deserialize_read_bytes_safe(&t.content_hash, bytes, out_bytes_read, bytes_size, sizeof(t.content_hash));
    deserialize_read_bytes_safe(&t.asset_start, bytes, out_bytes_read, bytes_size, sizeof(t.asset_start));
    deserialize_read_bytes_safe(&t.asset_size, bytes, out_bytes_read, bytes_size, sizeof(t.asset_size));
    deserialize_read_bytes_safe(&t.version, bytes, out_bytes_read, bytes_size, sizeof(t.version));
    deserialize_read_bytes_safe(&t.flags, bytes, out_bytes_read, bytes_size, sizeof(t.flags));
}

} // namespace assets
} // namespace eng