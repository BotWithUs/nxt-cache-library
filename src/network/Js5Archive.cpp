#include "network/Js5Archive.h"

#include <cstring>
#include <stdexcept>

namespace js5 {

void unpackNetworkArchive(const std::vector<uint8_t> &payload,
                          const std::vector<int> &subIds,
                          Archive &out)
{
    const size_t subCount = subIds.size();
    if (payload.empty()) return;

    if (subCount <= 1)
    {
        if (subCount == 1) out.files[subIds[0]].fileOffset = 0;
        out.data.writeFully(reinterpret_cast<char *>(const_cast<uint8_t *>(payload.data())),
                            payload.size());
        return;
    }

    int nchunks = payload[payload.size() - 1];
    if (nchunks <= 0)
    {
        throw std::runtime_error("Js5 archive: nchunks must be positive");
    }
    size_t footerLen = 4 * subCount * static_cast<size_t>(nchunks);
    if (footerLen + 1 > payload.size())
    {
        throw std::runtime_error("Js5 archive: footer overruns payload");
    }
    size_t footerStart = payload.size() - 1 - footerLen;

    std::vector<std::vector<int>> chunkSizes(nchunks, std::vector<int>(subCount));
    size_t scan = footerStart;
    for (int c = 0; c < nchunks; c++)
    {
        int last = 0;
        for (size_t f = 0; f < subCount; f++)
        {
            int delta = static_cast<int>(
                (static_cast<uint32_t>(payload[scan]) << 24) |
                (static_cast<uint32_t>(payload[scan + 1]) << 16) |
                (static_cast<uint32_t>(payload[scan + 2]) << 8) |
                 static_cast<uint32_t>(payload[scan + 3]));
            scan += 4;
            last += delta;
            chunkSizes[c][f] = last;
        }
    }

    std::vector<int> fileSizes(subCount, 0);
    for (int c = 0; c < nchunks; c++)
        for (size_t f = 0; f < subCount; f++) fileSizes[f] += chunkSizes[c][f];

    size_t total = 0;
    for (int s : fileSizes) total += static_cast<size_t>(s);

    std::vector<char> assembled(total);
    std::vector<int> fileStart(subCount, 0);
    int cur = 0;
    for (size_t f = 0; f < subCount; f++)
    {
        fileStart[f] = cur;
        cur += fileSizes[f];
    }

    if (nchunks == 1)
    {
        size_t srcOffset = 0;
        for (size_t f = 0; f < subCount; f++)
        {
            std::memcpy(assembled.data() + fileStart[f],
                        payload.data() + srcOffset,
                        chunkSizes[0][f]);
            srcOffset += chunkSizes[0][f];
        }
    }
    else
    {
        std::vector<int> writeCursor = fileStart;
        size_t srcOffset = 0;
        for (int c = 0; c < nchunks; c++)
        {
            for (size_t f = 0; f < subCount; f++)
            {
                int sz = chunkSizes[c][f];
                std::memcpy(assembled.data() + writeCursor[f],
                            payload.data() + srcOffset, sz);
                writeCursor[f] += sz;
                srcOffset += sz;
            }
        }
    }

    out.data.writeFully(assembled.data(), assembled.size());
    for (size_t f = 0; f < subCount; f++)
    {
        out.files[subIds[f]].fileOffset = fileStart[f];
    }
}

}  // namespace js5
