#pragma once

#include "gzip/config.hpp"

#include <zlib.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace gzip
{

class Decompressor
{
    std::size_t max_;

public:
    Decompressor(std::size_t max_bytes = 1000000000) // refuse if compressed data is > 1GB
        : max_(max_bytes)
    {
    }

    template<typename OutputType>
    void decompress(OutputType &output, const char *data, std::size_t size) const
    {
        z_stream inflate_s;

        inflate_s.zalloc = Z_NULL;
        inflate_s.zfree = Z_NULL;
        inflate_s.opaque = Z_NULL;
        inflate_s.avail_in = 0;
        inflate_s.next_in = Z_NULL;

        // -8..-15 raw deflate, 8..15 zlib, +16 gzip, +32 auto-detect gzip/zlib
        constexpr int window_bits = 15 + 32;

        if (inflateInit2(&inflate_s, window_bits) != Z_OK)
        {
            throw std::runtime_error("inflate init failed");
        }
        inflate_s.next_in = reinterpret_cast<z_const Bytef *>(data);

#ifdef DEBUG
        std::uint64_t size_64 = size * 2;
        if (size_64 > std::numeric_limits<unsigned int>::max())
        {
            inflateEnd(&inflate_s);
            throw std::runtime_error("size arg is too large to fit into unsigned int type x2");
        }
#endif
        if (size > max_ || (size * 2) > max_)
        {
            inflateEnd(&inflate_s);
            throw std::runtime_error("size may use more memory than intended when decompressing");
        }
        inflate_s.avail_in = static_cast<unsigned int>(size);
        std::size_t size_uncompressed = 0;
        do
        {
            std::size_t resize_to = size_uncompressed + 2 * size;
            if (resize_to > max_)
            {
                inflateEnd(&inflate_s);
                throw std::runtime_error("size of output string will use more memory than intended when decompressing");
            }
            output.resize(resize_to);
            inflate_s.avail_out = static_cast<unsigned int>(2 * size);
            inflate_s.next_out = reinterpret_cast<Bytef *>(&output[0] + size_uncompressed);
            int ret = inflate(&inflate_s, Z_FINISH);
            if (ret != Z_STREAM_END && ret != Z_OK && ret != Z_BUF_ERROR)
            {
                std::string error_msg = inflate_s.msg ? inflate_s.msg : "inflate failed";
                inflateEnd(&inflate_s);
                throw std::runtime_error(error_msg);
            }

            size_uncompressed += (2 * size - inflate_s.avail_out);
        } while (inflate_s.avail_out == 0);
        inflateEnd(&inflate_s);
        output.resize(size_uncompressed);
    }
};

inline std::string decompress(const char *data, std::size_t size)
{
    Decompressor decomp;
    std::string output;
    decomp.decompress(output, data, size);
    return output;
}

} // namespace gzip
