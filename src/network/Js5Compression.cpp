#include "network/Js5Compression.h"

#include "gzip/decompress.hpp"

#include <bzlib.h>

#include <cstring>
#include <stdexcept>

namespace js5 {

namespace {

uint32_t readU32BE(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

}  // namespace

std::vector<uint8_t> decompress(const uint8_t *data, size_t size)
{
    if (size < 5)
    {
        throw std::runtime_error("js5 decompress: buffer too small for compression header");
    }

    uint8_t type = data[0];
    uint32_t compressedSize = readU32BE(data + 1);

    if (type == 0)
    {
        if (size < 5 + compressedSize)
        {
            throw std::runtime_error("js5 decompress: truncated raw payload");
        }
        return std::vector<uint8_t>(data + 5, data + 5 + compressedSize);
    }

    if (size < 9)
    {
        throw std::runtime_error("js5 decompress: buffer too small for type+sizes");
    }
    uint32_t uncompressedSize = readU32BE(data + 5);
    if (size < 9 + compressedSize)
    {
        throw std::runtime_error("js5 decompress: truncated compressed payload");
    }

    if (type == 2)
    {
        // gzip-hpp auto-detects gzip vs zlib via window_bits = 15 + 32
        auto out = gzip::decompress(reinterpret_cast<const char *>(data + 9), compressedSize);
        if (out.size() != uncompressedSize)
        {
            throw std::runtime_error("js5 decompress: gzip uncompressed size mismatch");
        }
        return std::vector<uint8_t>(out.begin(), out.end());
    }

    if (type == 1)
    {
        // JS5 strips the 4-byte BZh8 header from the bzip2 stream — re-prepend it
        // before handing to libbz2.
        std::vector<uint8_t> framed(4 + compressedSize);
        framed[0] = 'B';
        framed[1] = 'Z';
        framed[2] = 'h';
        framed[3] = '0' + 8;  // block size 800k, matches what RuneScape uses
        std::memcpy(framed.data() + 4, data + 9, compressedSize);

        std::vector<uint8_t> out(uncompressedSize);
        unsigned int destLen = uncompressedSize;
        int rc = BZ2_bzBuffToBuffDecompress(
            reinterpret_cast<char *>(out.data()), &destLen,
            reinterpret_cast<char *>(framed.data()),
            static_cast<unsigned int>(framed.size()),
            /*small=*/0, /*verbosity=*/0);
        if (rc != BZ_OK)
        {
            throw std::runtime_error("js5 decompress: bzip2 BZ2_bzBuffToBuffDecompress rc=" +
                                      std::to_string(rc));
        }
        if (destLen != uncompressedSize)
        {
            throw std::runtime_error("js5 decompress: bzip2 size mismatch");
        }
        return out;
    }
    if (type == 3)
    {
        throw std::runtime_error("js5 decompress: lzma (type 3) not supported in this build");
    }
    throw std::runtime_error("js5 decompress: unknown compression type");
}

}  // namespace js5
