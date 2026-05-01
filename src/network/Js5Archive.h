#pragma once

#include "core/Archive.h"

#include <cstdint>
#include <vector>

namespace js5 {

// Unpack a JS5 network-format archive payload into `out`. The payload is the
// post-decompression body for the archive; subIds is the ascending list of
// fileIds (taken from the reference table). Populates out.data with concatenated
// files in subIds order and sets out.files[fid].fileOffset for each.
//
// Layout (rsmv unpackBufferArchive):
//   [files concatenated]
//   [N chunks of (subCount * 4-byte size deltas)]
//   [1 byte nchunks]
// Per-file size = sum of deltas across chunks for that fileindex.
// The single-file (subCount <= 1) case is special-cased: whole payload is the file.
//
// Throws std::runtime_error on malformed payload.
void unpackNetworkArchive(const std::vector<uint8_t> &payload,
                          const std::vector<int> &subIds,
                          Archive &out);

}  // namespace js5
