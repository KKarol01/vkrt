#include "asset_manager.hpp"
#include <ranges>
#include <zlib/zlib.h>
#include <eng/engine.hpp>
#include <eng/assets/loaders.hpp>
#include <eng/assets/serialization.hpp>

namespace eng
{
namespace assets
{

Asset null_asset{};

inline constexpr size_t INPUT_SIZE = 256 * 1024;

#define ZLIB_CHECK(error) ENG_ASSERT(error == Z_OK || error != Z_STREAM_ERROR, "Zlib failed with error {}", error)

const Asset& AssetManager::get_asset(const fs::Path& file_path)
{
    if(auto it = loaded_assets_map.find(file_path); it != loaded_assets_map.end()) { return it->second; }

    const auto ext = file_path.extension();

    const auto try_deserializing = [this, &file_path]() -> Asset* {
        auto engb_path = file_path;
        engb_path.replace_extension(".engb");
        auto engb_file = get_engine().fs->get_asset(engb_path, fs::OpenMode::RB);
        if(!engb_file) { return nullptr; }
        ENG_TIMER_SCOPED("Deserializing {}", engb_path.string());
        Asset asset{};
        z_stream strm{};
        inflateInit(&strm);
        unsigned char strm_buf[INPUT_SIZE];
        std::vector<std::byte> decompressed_asset;
        std::vector<std::byte> out_buf(INPUT_SIZE);
        int ret;
        do
        {
            const auto bytes_read = engb_file->read((std::byte*)strm_buf, INPUT_SIZE);
            if(bytes_read == 0) { break; }
            strm.next_in = strm_buf;
            strm.avail_in = bytes_read;
            do
            {
                strm.next_out = (Bytef*)out_buf.data();
                strm.avail_out = INPUT_SIZE;
                ret = inflate(&strm, Z_NO_FLUSH);
                if(ret != Z_OK && ret != Z_STREAM_END)
                {
                    ENG_WARN("Failed deserializing {} file", file_path.string());
                    inflateEnd(&strm);
                    return nullptr;
                }
                const auto have = INPUT_SIZE - strm.avail_out;
                decompressed_asset.insert(decompressed_asset.end(), out_buf.begin(), out_buf.begin() + have);
            }
            while(strm.avail_out == 0);
        }
        while(ret != Z_STREAM_END);
        inflateEnd(&strm);
        size_t read_bytes = 0;
        Serializer::deserialize((const std::byte*)decompressed_asset.data(), read_bytes, decompressed_asset.size(), asset);
        auto it = loaded_assets_map.emplace(file_path, std::move(asset));
        return &it.first->second;
    };

    if(auto* deserialized_asset = try_deserializing(); deserialized_asset) { return *deserialized_asset; }

    if(ext == ".glb" || ext == ".gltf")
    {
        auto asset = AssetLoaderGLTF::load_from_file(get_engine().fs->make_rel_path(file_path), ImportSettings::KEEP_DATA_BIT);
        if(!asset)
        {
            ENG_WARN("Couldn't load asset {}", file_path.string());
            return null_asset;
        }
        asset->path = file_path;
        auto it = loaded_assets_map.emplace(file_path, std::move(*asset));

        std::thread serialize_thread{ [&asset = it.first->second] {
            for(auto& signal : asset.geometry_data_futures)
            {
                signal.wait();
            }

            size_t size = 0;
            ENG_TIMER_SCOPED("Serializing asset {}", asset.path.string());
            Serializer::serialize(asset, size);
            if(size > 0)
            {
                std::vector<std::byte> asset_bytes(size);
                size = 0;
                Serializer::serialize(asset, size, asset_bytes.data(), asset_bytes.size());
                auto serialized_path = asset.path;
                serialized_path.replace_extension(".engb");

                z_stream strm{};
                auto err = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
                ZLIB_CHECK(err);

                size_t bytes_read = 0;
                size_t bytes_left = asset_bytes.size();
                unsigned char deflate_buffer[INPUT_SIZE];

                auto file = get_engine().fs->get_asset(serialized_path, fs::OpenMode::WB);
                if(!file)
                {
                    ENG_WARN("Could not open file {} for serializing", serialized_path.string());
                    return;
                }

                while(true)
                {
                    const size_t bytes_to_compress = std::min(INPUT_SIZE, bytes_left);
                    strm.avail_in = bytes_to_compress;
                    strm.next_in = (Bytef*)asset_bytes.data() + bytes_read;
                    bytes_read += bytes_to_compress;
                    bytes_left -= bytes_to_compress;
                    const auto flush = bytes_left == 0 ? Z_FINISH : Z_NO_FLUSH;
                    int ret;
                    do
                    {
                        strm.avail_out = INPUT_SIZE;
                        strm.next_out = deflate_buffer;

                        ret = deflate(&strm, flush);
                        ZLIB_CHECK(err);
                        const auto out_size = INPUT_SIZE - strm.avail_out;
                        if(out_size > 0)
                        {
                            if(file->write((const std::byte*)deflate_buffer, out_size) != out_size)
                            {
                                ENG_WARN("Could not write {} bytes to file {} during compression", out_size,
                                         serialized_path.string());
                                deflateEnd(&strm);
                                file->delete_from_disk();
                                return;
                            }
                        }
                    }
                    while(strm.avail_out == 0);
                    if(flush == Z_FINISH && ret == Z_STREAM_END) { break; }
                }
                deflateEnd(&strm);
            }

            asset.geometry_data.resize(0);
            asset.geometry_data_futures.resize(0);
            asset.image_data.resize(0);

            ENG_LOG("Serializing asset {} finished. Written {} bytes.", asset.path.string(), size);
        } };
        serialize_thread.detach();

        return it.first->second;
    }

    ENG_ERROR("Extension not supported {}", file_path.string());
    return null_asset;
}

} // namespace assets
} // namespace eng