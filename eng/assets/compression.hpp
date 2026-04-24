#pragma once

#include <type_traits>
#include <cstddef>
#include <span>
#include <zlib/zlib.h>

namespace eng
{
namespace compression
{

inline constexpr size_t ZLIB_SCRATCH_SIZE = 256 * 1024;

template <typename InputCallback, typename OutputCallback>
inline bool zlib_deflate(const InputCallback& input_callback, const OutputCallback& output_callback)
    requires(std::is_invocable_r_v<std::span<const std::byte>, InputCallback, size_t> &&
             std::is_invocable_v<OutputCallback, std::span<const std::byte>>)
{
    z_stream strm{};
    if(deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) { return false; }

    unsigned char scratch[ZLIB_SCRATCH_SIZE];
    bool success = true;
    int flush = Z_NO_FLUSH;

    while(true)
    {
        std::span<const std::byte> input = input_callback(ZLIB_SCRATCH_SIZE);
        strm.next_in = (Bytef*)input.data();
        strm.avail_in = input.size();
        if(input.empty()) { flush = Z_FINISH; }

        int ret;
        do
        {
            strm.next_out = scratch;
            strm.avail_out = ZLIB_SCRATCH_SIZE;
            ret = deflate(&strm, flush);
            if(ret == Z_STREAM_ERROR)
            {
                success = false;
                break;
            }
            const auto have = ZLIB_SCRATCH_SIZE - strm.avail_out;
            if(have > 0) { output_callback(std::as_bytes(std::span{ scratch, have })); }
        }
        while(strm.avail_out == 0);
        if(flush == Z_FINISH && ret == Z_STREAM_END) { break; }
        if(!success) { break; }
    }

    deflateEnd(&strm);
    return success;
}

template <typename InputCallback, typename OutputCallback>
inline bool zlib_inflate(const InputCallback& input_callback, const OutputCallback& output_callback)
    requires(std::is_invocable_r_v<std::span<const std::byte>, InputCallback, size_t> &&
             std::is_invocable_v<OutputCallback, std::span<const std::byte>>)
{
    z_stream strm{};
    if(inflateInit(&strm) != Z_OK) { return false; }

    unsigned char scratch[ZLIB_SCRATCH_SIZE];
    int ret = Z_OK;
    bool success = true;

    while(ret != Z_STREAM_END)
    {
        std::span<const std::byte> in_data = input_callback(ZLIB_SCRATCH_SIZE);
        if(in_data.empty()) { break; }
        strm.next_in = (Bytef*)in_data.data();
        strm.avail_in = in_data.size();
        do
        {
            strm.next_out = scratch;
            strm.avail_out = ZLIB_SCRATCH_SIZE;
            ret = inflate(&strm, Z_NO_FLUSH);
            if(ret < 0 && ret != Z_BUF_ERROR)
            {
                success = false;
                break;
            }
            const auto have = ZLIB_SCRATCH_SIZE - strm.avail_out;
            if(have > 0) { output_callback(std::as_bytes(std::span{ scratch, have })); }
        }
        while(strm.avail_out == 0);
        if(!success) { break; }
    }
    inflateEnd(&strm);
    return success && (ret == Z_STREAM_END);
}

} // namespace compression
} // namespace eng