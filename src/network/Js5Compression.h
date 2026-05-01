#pragma once

#include <cstdint>
#include <vector>

namespace js5 {

// Decompress a JS5 network-format blob (1-byte type + 4-byte size + optional 4-byte
// uncompressed size + payload). Type 0 = raw, type 2 = gzip.
// Throws std::runtime_error on unsupported types (1=bzip2, 3=lzma) or on decode failure.
std::vector<uint8_t> decompress(const uint8_t *data, size_t size);

}  // namespace js5
