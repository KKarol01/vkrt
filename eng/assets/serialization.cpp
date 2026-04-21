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

static void serialize_write_bytes_safe(void* dst, const void* src, size_t& dst_offset, size_t dst_size, size_t src_size)
{
    if(dst && src && dst_offset + src_size <= dst_size) { memcpy((std::byte*)dst + dst_offset, src, src_size); }
    dst_offset += src_size;
}

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

void Container::add_asset(uint8_t version, uint64_t custom_hash, std::span<const std::byte> asset)
{
    lists.emplace_back(custom_hash, ENG_HASH(asset), asset_bytes.size(), version);
    asset_bytes.insert(asset_bytes.end(), asset.begin(), asset.end());
}

void Container::serialize(size_t& out_bytes_written, std::byte* out_bytes, size_t out_bytes_size)
{
    static constexpr const char* MAGIC = "engb";
    static constexpr uint8_t VERSION = 0;
    serialize_write_bytes_safe(out_bytes, MAGIC, out_bytes_written, out_bytes_size, 4);
    serialize_write_bytes_safe(out_bytes, &VERSION, out_bytes_written, out_bytes_size, 1);

    const uint32_t item_count = (uint32_t)lists.size();
    serialize_write_bytes_safe(out_bytes, &item_count, out_bytes_written, out_bytes_size, 4);
    for(const auto& l : lists)
    {
        const size_t actual_start = HEADER_BYTE_SZ + (lists.size() * LIST_BYTE_SZ) + l.asset_start;
        serialize_write_bytes_safe(out_bytes, &l.custom_hash, out_bytes_written, out_bytes_size, 8);
        serialize_write_bytes_safe(out_bytes, &l.content_hash, out_bytes_written, out_bytes_size, 8);
        serialize_write_bytes_safe(out_bytes, &actual_start, out_bytes_written, out_bytes_size, 8);
        serialize_write_bytes_safe(out_bytes, &l.version, out_bytes_written, out_bytes_size, 1);
    }

    serialize_write_bytes_safe(out_bytes, asset_bytes.data(), out_bytes_written, out_bytes_size, asset_bytes.size());
}

void Container::serialize(size_t& out_bytes_written, fs::FilePtr out_file)
{
    if(!out_file) { return; }
    const auto total_size_bytes = HEADER_BYTE_SZ + (lists.size() * LIST_BYTE_SZ) + asset_bytes.size();
    std::vector<std::byte> data(total_size_bytes);
    serialize(out_bytes_written, data.data());
    ENG_ASSERT(total_size_bytes == out_bytes_written);
    out_file->write(data.data(), out_bytes_written);
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

        const uint64_t vertex_count = geom.vertices.size();
        serialize_write_bytes_safe(out_bytes, &vertex_count, out_bytes_written, out_bytes_size, sizeof(vertex_count));
        serialize_write_bytes_safe(out_bytes, &geom.vertex_layout, out_bytes_written, out_bytes_size, sizeof(geom.vertex_layout));
        serialize_write_bytes_safe(out_bytes, geom.vertices.data(), out_bytes_written, out_bytes_size,
                                   geom.vertices.size() * sizeof(float));

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

        uint64_t vertex_count = 0;
        deserialize_read_bytes_safe(&vertex_count, bytes, out_bytes_read, bytes_size, sizeof(vertex_count));
        deserialize_read_bytes_safe(&geom.vertex_layout, bytes, out_bytes_read, bytes_size, sizeof(geom.vertex_layout));
        geom.vertices.resize(vertex_count);
        deserialize_read_bytes_safe(geom.vertices.data(), bytes, out_bytes_read, bytes_size, vertex_count * sizeof(float));

        uint64_t meshlet_count = 0;
        deserialize_read_bytes_safe(&meshlet_count, bytes, out_bytes_read, bytes_size, sizeof(meshlet_count));
        geom.meshlet.resize(meshlet_count);
        deserialize_read_bytes_safe(geom.meshlet.data(), bytes, out_bytes_read, bytes_size,
                                    meshlet_count * sizeof(geom.meshlet[0]));

        const auto indices_u32 = geom.indices | std::views::transform([](auto idx16) { return (uint32_t)idx16; }) |
                                 std::ranges::to<std::vector<uint32_t>>();

        t.geometries[i] = gfx::get_renderer().make_geometry(gfx::GeometryDescriptor{
            .flags = {}, .vertex_layout = geom.vertex_layout, .vertices = geom.vertices, .indices = indices_u32, .meshlets = geom.meshlet });
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

} // namespace assets
} // namespace eng