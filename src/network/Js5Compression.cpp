#include "network/Js5Compression.h"

#include "gzip/decompress.hpp"

#include <bzlib.h>
#include <lzma.h>

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
        // JS5 LZMA payload: 5-byte LZMA1 properties header (lc/lp/pb + LE dict
        // size) followed by a raw LZMA1 stream with no length field — the output
        // size comes from the container's uncompressedSize. RS3 model groups
        // (cache index 47) use this.
        constexpr uint32_t kPropsSize = 5;
        if (compressedSize < kPropsSize)
        {
            throw std::runtime_error("js5 decompress: lzma payload too small for props");
        }
        const uint8_t *props = data + 9;
        const uint8_t *stream = props + kPropsSize;

        lzma_filter filters[2];
        filters[0].id = LZMA_FILTER_LZMA1;
        filters[0].options = nullptr;
        filters[1].id = LZMA_VLI_UNKNOWN;
        filters[1].options = nullptr;
        if (lzma_properties_decode(&filters[0], nullptr, props, kPropsSize) != LZMA_OK)
        {
            throw std::runtime_error("js5 decompress: lzma_properties_decode failed");
        }

        lzma_stream strm = LZMA_STREAM_INIT;
        lzma_ret rc = lzma_raw_decoder(&strm, filters);
        if (rc != LZMA_OK)
        {
            std::free(filters[0].options);
            throw std::runtime_error("js5 decompress: lzma_raw_decoder init failed rc=" +
                                     std::to_string(rc));
        }

        std::vector<uint8_t> out(uncompressedSize);
        strm.next_in   = stream;
        strm.avail_in  = compressedSize - kPropsSize;
        strm.next_out  = out.data();
        strm.avail_out = uncompressedSize;
        rc = lzma_code(&strm, LZMA_FINISH);
        lzma_end(&strm);
        std::free(filters[0].options);
        // LZMA1 raw streams have no end marker, so LZMA_STREAM_END isn't expected;
        // success is signalled by consuming the input and filling the output.
        if (rc != LZMA_OK && rc != LZMA_STREAM_END)
        {
            throw std::runtime_error("js5 decompress: lzma_code failed rc=" + std::to_string(rc));
        }
        if (strm.avail_out != 0)
        {
            throw std::runtime_error("js5 decompress: lzma produced " +
                                     std::to_string(uncompressedSize - strm.avail_out) +
                                     " of " + std::to_string(uncompressedSize) + " bytes");
        }
        return out;
    }
    throw std::runtime_error("js5 decompress: unknown compression type");
}

}  // namespace js5
