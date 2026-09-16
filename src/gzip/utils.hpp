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

#include <cstdint>
#include <cstdlib>

namespace gzip
{

inline bool is_compressed(const char *data, std::size_t size)
{
    return size > 2 &&
           (
               // zlib
               (
                   static_cast<uint8_t>(data[0]) == 0x78 &&
                   (static_cast<uint8_t>(data[1]) == 0x9C ||
                    static_cast<uint8_t>(data[1]) == 0x01 ||
                    static_cast<uint8_t>(data[1]) == 0xDA ||
                    static_cast<uint8_t>(data[1]) == 0x5E)) ||
               // gzip
               (static_cast<uint8_t>(data[0]) == 0x1F && static_cast<uint8_t>(data[1]) == 0x8B));
}

} // namespace gzip
