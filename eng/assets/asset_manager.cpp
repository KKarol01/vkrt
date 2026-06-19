#include "asset_manager.hpp"
#include <ranges>
#include <eng/engine.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/assets/loaders.hpp>
#include <eng/assets/serialization.hpp>
#include <eng/assets/compression.hpp>

namespace eng
{

using namespace serialization;

namespace assets
{

Asset s_null_asset{};

void AssetManager::init()
{
    std::vector<fs::Path> engb_paths;
    for(auto it : std::filesystem::directory_iterator{ get_engine().fs->make_rel_path("/assets") })
    {
        if(it.path().extension() == ".engb")
        {
            auto itpath = it.path();
            itpath.make_preferred();
            engb_paths.push_back(itpath);
        }
    }
    std::sort(engb_paths.begin(), engb_paths.end(), [](const auto& a, const auto& b) { return b < a; });
    m_engb_containers_vec.reserve(engb_paths.size());
    for(const auto& p : engb_paths)
    {
        m_engb_containers_vec.emplace_back(get_engine().fs->open_file(p, fs::OpenMode::READ_WRITE_BYTES));
    }
}

const Asset& AssetManager::get_asset(const fs::Path& file_path)
{
    if(auto it = m_loaded_assets_map.find(file_path); it != m_loaded_assets_map.end()) { return it->second; }

    const auto ext = file_path.extension();

    if(auto assetopt = try_deserialize_asset(file_path); assetopt)
    {
        auto it = m_loaded_assets_map.emplace(file_path, std::move(*assetopt));
        return it.first->second;
    }

    if(ext == ".glb" || ext == ".gltf")
    {
        auto asset = AssetLoaderGLTF::load_from_file(get_engine().fs->make_rel_path(file_path), ImportSettings::KEEP_DATA_BIT);
        if(!asset)
        {
            ENG_WARN("Couldn't load asset {}", file_path.string());
            return s_null_asset;
        }
        asset->path = file_path;
        auto it = m_loaded_assets_map.emplace(file_path, std::move(*asset));

        if(get_engine().settings.serialize_to_enbc)
        {
            m_serializing_threads.push_back(std::make_unique<std::jthread>(std::jthread{
                &AssetManager::serialize_asset_to_enbc_thread, this, std::ref(it.first->second) }));
        }

        return it.first->second;
    }

    ENG_ERROR("Extension not supported {}", file_path.string());
    return s_null_asset;
}

serialization::engb::Container& AssetManager::get_latest_container()
{
    if(m_engb_containers_vec.empty())
    {
        m_engb_containers_vec.emplace_back(get_engine().fs->get_asset("/assets/assets0.engb", fs::OpenMode::READ_WRITE_CREATE_BYTES));
    }
    return m_engb_containers_vec.front();
}

std::optional<serialization::engb::List> AssetManager::try_find_list_by_hash(u64 hash, serialization::engb::Container** out_container)
{
    std::shared_lock lock{ m_engbc_vec_mutex };
    for(auto& c : m_engb_containers_vec)
    {
        const auto list = c.get_asset_list(hash);
        if(list)
        {
            if(out_container) { *out_container = &c; }
            return list;
        }
    }
    return std::nullopt;
}

std::optional<Asset> AssetManager::try_deserialize_asset(const fs::Path& file_path)
{
    {
        u32 head{};
        while(m_serializing_threads.size())
        {
            if(m_serializing_threads[head]->joinable()) { break; }
            ++head;
        }
        m_serializing_threads.erase(m_serializing_threads.begin(), m_serializing_threads.begin() + head);
    }
    serialization::engb::Container* container{};
    const auto listopt = try_find_list_by_hash(ENG_HASH(file_path.string()), &container);
    if(!listopt) { return std::nullopt; }
    ENG_ASSERT(container);
    const auto& list = *listopt;

    if(list.version != 0) { ENG_WARN("Asset {} has invalid version {}", file_path.string(), list.version); }

    ENG_TIMER_SCOPED("Deserializing {}", file_path.string());
    std::shared_lock lock{ m_engbc_vec_mutex };

    std::vector<std::byte> asset_bytes;
    if(list.flags.test(engb::ListFlags::CONTENT_COMPRESSED_BIT))
    {
        std::vector<std::byte> file_buf(compression::ZLIB_SCRATCH_SIZE);
        size_t asset_read_offset = 0;
        u64 uncompressed_size = 0;
        container->m_file->read((std::byte*)&uncompressed_size, 8, list.asset_start - 8);
        asset_bytes.reserve(uncompressed_size);
        auto success = compression::zlib_inflate(
            [&](size_t size) {
                const auto file_read = container->get_asset_data(list, std::span{ file_buf.data(), size }, asset_read_offset);
                asset_read_offset += file_read;
                const auto bytes_to_process = std::min(file_read, size);
                auto span = std::span<const std::byte>{ file_buf.data(), bytes_to_process };
                return span;
            },
            [&](std::span<const std::byte> data) { asset_bytes.insert(asset_bytes.end(), data.begin(), data.end()); });
        if(!success)
        {
            ENG_WARN("Failed to decompress serialized data {}", file_path.string());
            return std::nullopt;
        }
    }
    else
    {
        asset_bytes.resize(list.asset_size);
        const auto n_bytesread = container->get_asset_data(list, std::span{ asset_bytes }, 0);
        if(n_bytesread != list.asset_size)
        {
            ENG_WARN("Could not read asset bytes.");
            return std::nullopt;
        }
    }

    size_t read_bytes = 0;
    Asset asset{};
    deserialize(asset, std::span{ asset_bytes }, read_bytes);
    return asset;
}

void AssetManager::serialize_asset_to_enbc_thread(Asset& asset)
{
    if(asset.geometry_data_futures.empty())
    {
        ENG_WARN("Cannot serialize asset without geometries");
        return;
    }

    for(auto& signal : asset.geometry_data_futures)
    {
        signal.wait();
    }

    size_t size = 0;
    ENG_TIMER_SCOPED("Serializing asset {}", asset.path.string());
    std::vector<std::byte> asset_bytes;
    serialize(std::span{ asset_bytes }, asset, size);

    if(size > 0)
    {
        asset_bytes.resize(size);
        size = 0;
        serialize(std::span{ asset_bytes }, asset, size);

        size_t bytes_read = 0;
        size_t bytes_left = asset_bytes.size();

        std::scoped_lock lock{ m_engbc_vec_mutex };
        auto& engbc = get_latest_container();
        // engbc.add_asset(0, ENG_HASH(asset.path.string()), engb::ListFlags::CONTENT_COMPRESSED_BIT, {}, engb::AssetMetadata{ .uncompressed_size = asset_bytes.size() });
        engbc.add_asset(0, ENG_HASH(asset.path.string()), {}, {}, engb::AssetMetadata{ .uncompressed_size = asset_bytes.size() });
        engbc.append_asset_bytes(std::span{ asset_bytes }, true);

#if 0
        size_t bytes_compressed = 0;
        const auto res = compression::zlib_deflate(
            [&](size_t size) {
                const auto bytes_to_process = std::min(bytes_left, size);
                auto span = std::span<const std::byte>{ asset_bytes.data() + bytes_read, bytes_to_process };
                bytes_read += bytes_to_process;
                bytes_left -= bytes_to_process;
                return span;
            },
            [&](std::span<const std::byte> data) {
                bytes_compressed += data.size();
                engbc.append_asset_bytes(data, false);
            });

        ENG_ASSERT(res, "Zlib compression failed {}", asset.path.string());
        engbc.append_asset_bytes({}, true);
#endif
    }

    asset.geometry_data.resize(0);
    asset.geometry_data_futures.resize(0);
    asset.image_data.resize(0);

    ENG_LOG("Serializing asset {} finished. Written {} bytes.", asset.path.string(), size);
}

} // namespace assets

namespace serialization
{
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

    serialize(dst, (u64)src.textures.size(), out_bytes_written);
    for(auto txt : src.textures)
    {
        // store index to image array so it can later be deserialized
        auto it = std::ranges::find(src.images, txt.image);
        ENG_ASSERT(it != src.images.end());
        *txt.image = (u64)std::distance(src.images.begin(), it);
        ENG_ASSERT(*txt.image < src.images.size());
        serialize(dst, txt, out_bytes_written);
    }

    ENG_ASSERT(src.geometries.size() == src.geometry_data.size());
    serialize(dst, src.geometry_data.size(), out_bytes_written);
    for(const auto& gdf : src.geometry_data_futures)
    {
        const auto& gd = gdf.get();
        serialize(dst, gd, out_bytes_written);
    }

    serialize(dst, (u64)src.materials.size(), out_bytes_written);
    for(const auto& math : src.materials)
    {
        auto mat = math.get();
        // remap image handles to store indices to t.images array, so they can be safely deserialized.
        if(mat.base_color_texture)
        {
            auto idx = std::distance(src.images.begin(), std::ranges::find(src.images, mat.base_color_texture.image));
            *mat.base_color_texture.image = (u64)idx;
            if(idx == src.images.size()) { mat.base_color_texture.image = {}; }
        }
        if(mat.normal_texture)
        {
            *mat.normal_texture.image =
                (u64)std::distance(src.images.begin(), std::ranges::find(src.images, mat.normal_texture.image));
        }
        if(mat.metallic_roughness_texture)
        {
            *mat.metallic_roughness_texture.image =
                (u64)std::distance(src.images.begin(), std::ranges::find(src.images, mat.metallic_roughness_texture.image));
        }
        mat.mesh_pass = {}; // ignoring meshpass, as load_material uses default one
        serialize(dst, mat, out_bytes_written);
    }

    serialize(dst, (u64)src.meshes.size(), out_bytes_written);
    for(const auto& meshh : src.meshes)
    {
        auto mesh = meshh.get();
        *mesh.geometry = (u64)std::distance(src.geometries.begin(), std::ranges::find(src.geometries, mesh.geometry));
        if(mesh.material)
        {
            *mesh.material = (u64)std::distance(src.materials.begin(), std::ranges::find(src.materials, mesh.material));
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

    u64 image_count;
    deserialize(image_count, src, out_bytes_written);
    dst.images.resize(image_count);
    assets::ParsedImageData imgd{};
    auto* mip_cmd = gfx::get_renderer().current_data->cmdpool->begin();
    for(auto i = 0u; i < image_count; ++i)
    {
        // don't use deserialize() here, because it would double the image data for no reason, we can read from the stream
        u64 pixel_data_size = 0;
        deserialize(imgd.name, src, out_bytes_written);
        deserialize(imgd.width, src, out_bytes_written);
        deserialize(imgd.height, src, out_bytes_written);
        deserialize(imgd.format, src, out_bytes_written);
        deserialize(pixel_data_size, src, out_bytes_written);
        dst.images[i] =
            gfx::get_renderer().make_image(imgd.name, gfx::Image::init(imgd.width, imgd.height, 0, imgd.format,
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
            gfx::get_renderer().staging->copy(dst.images[i].get(), src.data() + out_bytes_written, 0, 0, false,
                                              gfx::DiscardContents::YES);
            mip_cmd->generate_mips(dst.images[i].get());
        }
        out_bytes_written += pixel_data_size;
    }
    gfx::get_renderer().current_data->cmdpool->end(mip_cmd);
    auto* mip_sync = gfx::get_renderer().current_data->get_sync();
    mip_cmd->wait_sync(gfx::get_renderer().staging->flush(true), gfx::PipelineStage::TRANSFER_BIT);
    mip_cmd->signal_sync(mip_sync, gfx::PipelineStage::FRAGMENT_BIT);
    gfx::get_renderer().gq->with_cmd_buf(mip_cmd).submit();
    gfx::get_renderer().current_data->wait_syncs.push_back(mip_sync);

    u64 texture_count;
    deserialize(texture_count, src, out_bytes_written);
    dst.textures.resize(texture_count);
    for(auto& txt : dst.textures)
    {
        deserialize(txt, src, out_bytes_written);
        txt.image = dst.images[(u32)*txt.image]; // when serializing, handles are made into indices into this array
    }

    u64 geom_count;
    deserialize(geom_count, src, out_bytes_written);
    dst.geometries.resize(geom_count);
    assets::ParsedGeometryData geom;
    for(u64 i = 0; i < geom_count; ++i)
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

    u64 material_count;
    deserialize(material_count, src, out_bytes_written);
    dst.materials.resize(material_count);
    gfx::Material mat;
    for(u64 i = 0; i < material_count; ++i)
    {
        deserialize(mat, src, out_bytes_written);
        if(mat.base_color_texture) { mat.base_color_texture = dst.textures[(u32)*mat.base_color_texture.image]; }
        if(mat.normal_texture) { mat.normal_texture = dst.textures[(u32)*mat.normal_texture.image]; }
        if(mat.metallic_roughness_texture)
        {
            mat.metallic_roughness_texture = dst.textures[(u32)*mat.metallic_roughness_texture.image];
        }
        dst.materials[i] = gfx::get_renderer().make_material(mat);
    }

    u64 mesh_count = 0;
    deserialize(mesh_count, src, out_bytes_written);
    dst.meshes.resize(mesh_count);
    gfx::Mesh mesh;
    for(u64 i = 0; i < mesh_count; ++i)
    {
        deserialize(mesh, src, out_bytes_written);
        if(mesh.material) { mesh.material = dst.materials[(u32)*mesh.material]; }
        if(mesh.geometry) { mesh.geometry = dst.geometries[(u32)*mesh.geometry]; }
        dst.meshes[i] = gfx::get_renderer().make_mesh(gfx::MeshDescriptor{ mesh.geometry, mesh.material });
    }

    deserialize(dst.nodes, src, out_bytes_written);
    deserialize(dst.transforms, src, out_bytes_written);
    deserialize(dst.root_nodes, src, out_bytes_written);
}

} // namespace serialization

} // namespace eng