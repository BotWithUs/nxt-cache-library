#pragma once

#include "core/Archive.h"

#include <map>
#include <vector>

namespace js5 {

class Js5Socket;

// Per-major view backed by the live JS5 protocol. Construction fetches and parses
// the reference table for `id`. archive(aid) lazily fetches + decompresses + unpacks
// the requested archive in network format, caches the resulting Archive, and returns it.
class Js5Index
{
public:
    int id;
    int version{};
    std::vector<int> archiveIds;
    std::map<int, Archive> archives;

    Js5Index(int id, Js5Socket *socket);

    Archive &archive(int archiveId);

private:
    struct ArchiveMeta
    {
        int crc{};
        int version{};
        std::vector<int> subIds;        // delta-decoded subfile ids
        std::vector<int> subNameHashes; // empty if reference table has no name flag
    };

    void loadReferenceTable();
    void decodeReferenceTable(const std::vector<uint8_t> &refBytes);

    Js5Socket *socket_;
    std::map<int, ArchiveMeta> meta_;
};

}  // namespace js5
