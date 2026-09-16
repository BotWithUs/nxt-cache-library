// Vendored from mapbox/gzip-hpp - https://github.com/mapbox/gzip-hpp
//
// Copyright (c) 2017, Mapbox Inc.
// All rights reserved.
//
// Licensed under the BSD 2-Clause License. Redistributions of source code
// must retain the above copyright notice, this list of conditions and the
// disclaimer reproduced in full in THIRD_PARTY_NOTICES.md at the repository
// root, which accompanies this distribution.
//
// Reformatted to this project's brace style; otherwise unmodified.

#pragma once

#include "gzip/config.hpp"

#include <zlib.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace gzip
{

class Compressor
{
    std::size_t max_;
    int level_;

public:
    Compressor(int level = Z_DEFAULT_COMPRESSION,
               std::size_t max_bytes = 2000000000)
        : max_(max_bytes), level_(level)
    {
    }

    template<typename InputType>
    void compress(InputType &output, const char *data, std::size_t size) const
    {
#ifdef DEBUG
        if (size > std::numeric_limits<unsigned int>::max())
        {
            throw std::runtime_error("size arg is too large to fit into unsigned int type");
        }
#endif
        if (size > max_)
        {
            throw std::runtime_error("size may use more memory than intended when decompressing");
        }

        z_stream deflate_s;
        deflate_s.zalloc = Z_NULL;
        deflate_s.zfree = Z_NULL;
        deflate_s.opaque = Z_NULL;
        deflate_s.avail_in = 0;
        deflate_s.next_in = Z_NULL;

        constexpr int window_bits = 15 + 16;
        constexpr int mem_level = 8;

        if (deflateInit2(&deflate_s, level_, Z_DEFLATED, window_bits, mem_level, Z_DEFAULT_STRATEGY) != Z_OK)
        {
            throw std::runtime_error("deflate init failed");
        }

        deflate_s.next_in = reinterpret_cast<z_const Bytef *>(data);
        deflate_s.avail_in = static_cast<unsigned int>(size);

        std::size_t size_compressed = 0;
        do
        {
            size_t increase = size / 2 + 1024;
            if (output.size() < (size_compressed + increase))
            {
                output.resize(size_compressed + increase);
            }
            deflate_s.avail_out = static_cast<unsigned int>(increase);
            deflate_s.next_out = reinterpret_cast<Bytef *>(&output[0] + size_compressed);
            deflate(&deflate_s, Z_FINISH);
            size_compressed += (increase - deflate_s.avail_out);
        } while (deflate_s.avail_out == 0);

        deflateEnd(&deflate_s);
        output.resize(size_compressed);
    }
};

inline std::string compress(const char *data, std::size_t size, int level = Z_DEFAULT_COMPRESSION)
{
    Compressor comp(level);
    std::string output;
    comp.compress(output, data, size);
    return output;
}

} // namespace gzip
