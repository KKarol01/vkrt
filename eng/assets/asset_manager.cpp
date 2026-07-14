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

#if 0
namespace serialization
{
template <> void serialize<assets::Asset>(std::span<std::byte> dst, const assets::Asset& src, usize& out_bytes_written)
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
            if(idx == src.images.size()) {}
            *mat.base_color_texture.image = idx;
            if(idx == src.images.size()) { mat.base_color_texture.image = {}; }
        }
        if(mat.normal_texture)
        {
            *mat.normal_texture.image =
                std::distance(src.images.begin(), std::ranges::find(src.images, mat.normal_texture.image));
        }
        if(mat.metallic_roughness_texture)
        {
            *mat.metallic_roughness_texture.image =
                std::distance(src.images.begin(), std::ranges::find(src.images, mat.metallic_roughness_texture.image));
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
void deserialize<assets::Asset>(assets::Asset& dst, std::span<const std::byte> src, usize& out_bytes_written)
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
        txt.image = dst.images[*txt.image]; // when serializing, handles are made into indices into this array
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
#endif

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
        m_engb_containers_vec.emplace_back(get_engine().fs->open_file(p, fs::OpenMode::TRY_READ_WRITE_BYTES_BEG));
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
        m_engb_containers_vec.emplace_back(get_engine().fs->open_file("/assets/assets0.engb",
                                                                      fs::OpenMode::READ_WRITE_BYTES_CREATE_DISCARD));
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

    if(list.version != 0)
    {
        ENG_WARN("Asset {} has invalid version {}", file_path.string(), list.version);
        return std::nullopt;
    }

    ENG_TIMER_SCOPED("Deserializing {}", file_path.string());
    std::shared_lock lock{ m_engbc_vec_mutex };

    std::vector<std::byte> asset_bytes;
    if(list.flags.test(engb::ListFlags::CONTENT_COMPRESSED_BIT))
    {
        std::vector<std::byte> file_buf(compression::ZLIB_SCRATCH_SIZE);
        usize asset_read_offset = 0;
        u64 uncompressed_size = 0;
        usize n_bytes_read = 0;

        // Add N_HEADER_BYTES to convert payload-relative offset to absolute file offset
        const u64 meta_offset = serialization::engb::v0::N_HEADER_BYTES + list.asset_start - sizeof(u64);
        container->m_file->read(reinterpret_cast<std::byte*>(&uncompressed_size), sizeof(u64), n_bytes_read, meta_offset);

        asset_bytes.reserve(uncompressed_size);
        auto success = compression::zlib_inflate(
            [&](usize size) {
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

    Asset asset{};
    serialization::Context ctx(std::span{ asset_bytes }, 0);
    asset.deserialize(ctx);
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

    ENG_TIMER_SCOPED("Serializing asset {}", asset.path.string());

    // empty context to calc the size required.
    serialization::Context ctx{std::span<std::byte>{}, 0};
    asset.serialize(ctx);
    const usize required_size = ctx.m_offset;

    if(required_size > 0)
    {
        // 2. Real serialization pass
        std::vector<std::byte> asset_bytes(required_size);
        serialization::Context ctx(std::span{ asset_bytes }, 0);
        asset.serialize(ctx);
        ENG_ASSERT(ctx.m_offset == required_size);

        std::scoped_lock lock{ m_engbc_vec_mutex };
        auto& engbc = get_latest_container();

#if 0
        // Clean streaming compression implementation
        engbc.add_asset(0, ENG_HASH(asset.path.string()), engb::ListFlags::CONTENT_COMPRESSED_BIT, {}, 
                        engb::AssetMetadata{ .uncompressed_size = asset_bytes.size() });
        
        usize bytes_read = 0;
        usize bytes_left = asset_bytes.size();
        const auto res = compression::zlib_deflate(
            [&](usize size) {
                const auto bytes_to_process = std::min(bytes_left, size);
                auto span = std::span<const std::byte>{ asset_bytes.data() + bytes_read, bytes_to_process };
                bytes_read += bytes_to_process;
                bytes_left -= bytes_to_process;
                return span;
            },
            [&](std::span<const std::byte> data) {
                engbc.append_asset_bytes(data, false);
            });

        ENG_ASSERT(res, "Zlib compression failed {}", asset.path.string());
        engbc.append_asset_bytes({}, true);
#else
        engbc.add_asset(0, ENG_HASH(asset.path.string()), {}, {}, engb::AssetMetadata{ .uncompressed_size = asset_bytes.size() });
        engbc.append_asset_bytes(std::span{ asset_bytes }, true);
#endif
    }

    asset.geometry_data.resize(0);
    asset.geometry_data_futures.resize(0);
    asset.image_data.resize(0);

    ENG_LOG("Serializing asset {} finished. Written {} bytes.", asset.path.string(), required_size);
}

void Asset::serialize(serialization::Context& ctx) const
{
    if(geometry_data.empty())
    {
        ENG_ASSERT(false,
                   "Geometry data for asset {} must not be empty when serializing. Did you forget to provide required "
                   "keep data flag when importing?",
                   path.string());
        return;
    }

    const std::string path_str = path.string();
    ctx.serialize(path_str);

    ENG_ASSERT(images.size() == image_data.size());
    ctx.serialize(image_data);

    const u64 tex_count = textures.size();
    ctx.serialize(tex_count);
    for(auto txt : textures)
    {
        auto it = std::ranges::find(images, txt.image);
        ENG_ASSERT(it != images.end());
        *txt.image = (u64)std::distance(images.begin(), it);
        ENG_ASSERT(*txt.image < images.size());
        ctx.serialize(txt);
    }

    ENG_ASSERT(geometries.size() == geometry_data.size());
    const u64 geom_count = geometry_data_futures.size();
    ctx.serialize(geom_count);
    for(const auto& gdf : geometry_data_futures)
    {
        const auto& gd = gdf.get();
        ctx.serialize(gd);
    }

    const u64 mat_count = materials.size();
    ctx.serialize(mat_count);
    for(const auto& math : materials)
    {
        auto mat = math.get();
        if(mat.base_color_texture)
        {
            auto idx = std::distance(images.begin(), std::ranges::find(images, mat.base_color_texture.image));
            *mat.base_color_texture.image = idx;
            if(idx == images.size()) { mat.base_color_texture.image = {}; }
        }
        if(mat.normal_texture)
        {
            *mat.normal_texture.image = std::distance(images.begin(), std::ranges::find(images, mat.normal_texture.image));
        }
        if(mat.metallic_roughness_texture)
        {
            *mat.metallic_roughness_texture.image =
                std::distance(images.begin(), std::ranges::find(images, mat.metallic_roughness_texture.image));
        }
        mat.mesh_pass = {};
        ctx.serialize(mat);
    }

    const u64 mesh_count = meshes.size();
    ctx.serialize(mesh_count);
    for(const auto& meshh : meshes)
    {
        auto mesh = meshh.get();
        *mesh.geometry = (u64)std::distance(geometries.begin(), std::ranges::find(geometries, mesh.geometry));
        if(mesh.material)
        {
            *mesh.material = (u64)std::distance(materials.begin(), std::ranges::find(materials, mesh.material));
        }
        ctx.serialize(mesh);
    }

    ctx.serialize(nodes);
    ctx.serialize(transforms);
    ctx.serialize(root_nodes);
}

void Asset::deserialize(serialization::Context& ctx)
{
    std::string pathstr;
    ctx.deserialize(pathstr);
    path = pathstr;
    ENG_ASSERT(!path.empty());

    u64 image_count = 0;
    ctx.deserialize(image_count);
    images.resize(image_count);
    assets::ParsedImageData imgd{};
    auto* mip_cmd = gfx::get_renderer().current_data->cmdpool->begin();
    for(auto i = 0u; i < image_count; ++i)
    {
        u64 pixel_data_size = 0;
        ctx.deserialize(imgd.name);
        ctx.deserialize(imgd.width);
        ctx.deserialize(imgd.height);
        ctx.deserialize(imgd.format);
        ctx.deserialize(pixel_data_size);

        images[i] = gfx::get_renderer().make_image(imgd.name, gfx::Image::init(imgd.width, imgd.height, 0, imgd.format,
                                                                               gfx::ImageUsage::SAMPLED_BIT | gfx::ImageUsage::TRANSFER_DST_BIT |
                                                                                   gfx::ImageUsage::TRANSFER_SRC_BIT,
                                                                               0, 1, gfx::ImageLayout::READ_ONLY));
        if(!images[i])
        {
            ENG_WARN("Failed to create image {}", imgd.name.empty() ? "EMPTY IMAGE NAME" : imgd.name.as_view());
            ctx.m_offset += pixel_data_size;
            continue;
        }
        else
        {
            // Zero-copy read directly from the streaming context buffer
            gfx::get_renderer().staging->copy(images[i].get(), ctx.m_bytes.data() + ctx.m_offset, 0, 0, false,
                                              gfx::DiscardContents::YES);
            mip_cmd->generate_mips(images[i].get());
        }
        ctx.m_offset += pixel_data_size;
    }
    gfx::get_renderer().current_data->cmdpool->end(mip_cmd);
    auto* mip_sync = gfx::get_renderer().current_data->get_sync();
    mip_cmd->wait_sync(gfx::get_renderer().staging->flush(true), gfx::PipelineStage::TRANSFER_BIT);
    mip_cmd->signal_sync(mip_sync, gfx::PipelineStage::FRAGMENT_BIT);
    gfx::get_renderer().gq->with_cmd_buf(mip_cmd).submit();
    gfx::get_renderer().current_data->wait_syncs.push_back(mip_sync);

    u64 texture_count = 0;
    ctx.deserialize(texture_count);
    textures.resize(texture_count);
    for(auto& txt : textures)
    {
        ctx.deserialize(txt);
        txt.image = images[(u32)*txt.image];
    }

    u64 geom_count = 0;
    ctx.deserialize(geom_count);
    geometries.resize(geom_count);
    assets::ParsedGeometryData geom;
    for(u64 i = 0; i < geom_count; ++i)
    {
        ctx.deserialize(geom);
        geometries[i] =
            gfx::get_renderer().make_geometry(gfx::GeometryDescriptor{ .flags = {},
                                                                       .vertex_layout = geom.vertex_layout,
                                                                       .index_format = gfx::IndexFormat::U16,
                                                                       .vertices = geom.positions,
                                                                       .attributes = geom.attributes,
                                                                       .indices = std::as_bytes(std::span{ geom.indices }),
                                                                       .meshlets = geom.meshlets });
    }

    u64 material_count = 0;
    ctx.deserialize(material_count);
    materials.resize(material_count);
    gfx::Material mat;
    for(u64 i = 0; i < material_count; ++i)
    {
        ctx.deserialize(mat);
        if(mat.base_color_texture) { mat.base_color_texture = textures[(u32)*mat.base_color_texture.image]; }
        if(mat.normal_texture) { mat.normal_texture = textures[(u32)*mat.normal_texture.image]; }
        if(mat.metallic_roughness_texture)
        {
            mat.metallic_roughness_texture = textures[(u32)*mat.metallic_roughness_texture.image];
        }
        materials[i] = gfx::get_renderer().make_material(mat);
    }

    u64 mesh_count = 0;
    ctx.deserialize(mesh_count);
    meshes.resize(mesh_count);
    gfx::Mesh mesh;
    for(u64 i = 0; i < mesh_count; ++i)
    {
        ctx.deserialize(mesh);
        if(mesh.material) { mesh.material = materials[(u32)*mesh.material]; }
        if(mesh.geometry) { mesh.geometry = geometries[(u32)*mesh.geometry]; }
        meshes[i] = gfx::get_renderer().make_mesh(gfx::MeshDescriptor{ mesh.geometry, mesh.material });
    }

    ctx.deserialize(nodes);
    ctx.deserialize(transforms);
    ctx.deserialize(root_nodes);
}

} // namespace assets

} // namespace eng