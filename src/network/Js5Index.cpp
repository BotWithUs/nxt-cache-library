#include "network/Js5Index.h"

#include "core/RSBuffer.h"
#include "network/Js5Archive.h"
#include "network/Js5Compression.h"
#include "network/Js5Socket.h"

#include <stdexcept>

namespace js5 {

Js5Index::Js5Index(int id, Js5Socket *socket) : id(id), socket_(socket)
{
    loadReferenceTable();
}

void Js5Index::loadReferenceTable()
{
    auto raw = socket_->getFile(255, id);
    auto decompressed = js5::decompress(raw.data(), raw.size());
    decodeReferenceTable(decompressed);
}

void Js5Index::decodeReferenceTable(const std::vector<uint8_t> &refBytes)
{
    // Mirrors Index::decodeReferenceBlob, but operates on a raw byte vector.
    RSBuffer buffer(reinterpret_cast<char *>(const_cast<uint8_t *>(refBytes.data())),
                    refBytes.size());

    unsigned char format = buffer.readUnsignedByte();
    if (format < 5 || format > 7)
    {
        throw std::runtime_error("Js5 reference table: invalid format " +
                                  std::to_string(format));
    }
    version = format >= 6 ? buffer.readInt() : 0;
    int mask = buffer.readUnsignedByte();

    bool hasNames      = (mask & 0x1) != 0;
    bool hasWhirlpools = (mask & 0x2) != 0;
    bool hasSizes      = (mask & 0x4) != 0;
    bool hasHashes     = (mask & 0x8) != 0;

    auto readFormat = [&]() -> int {
        return format >= 7 ? buffer.readSmartInt() : buffer.readUnsignedShort();
    };

    int archiveCount = readFormat();
    archiveIds.resize(archiveCount);
    for (int i = 0; i < archiveCount; i++)
    {
        int delta = readFormat();
        int aid = delta + (i == 0 ? 0 : archiveIds[i - 1]);
        archiveIds[i] = aid;
        archives[aid] = Archive(aid);
        meta_[aid] = ArchiveMeta{};
    }
    if (hasNames)
    {
        for (int aid : archiveIds) archives[aid].nameHash = buffer.readInt();
    }
    for (int aid : archiveIds)
    {
        int crc = buffer.readInt();
        archives[aid].crc = crc;
        meta_[aid].crc = crc;
    }
    if (hasHashes)
    {
        for (int aid : archiveIds) archives[aid].hash = buffer.readInt();
    }
    if (hasWhirlpools)
    {
        for (int aid : archiveIds) buffer.read(archives[aid].whirlpool, 64);
    }
    if (hasSizes)
    {
        for (int aid : archiveIds)
        {
            archives[aid].compressedSize = buffer.readInt();
            archives[aid].uncompressedSize = buffer.readInt();
        }
    }
    for (int aid : archiveIds)
    {
        int v = buffer.readInt();
        archives[aid].version = v;
        meta_[aid].version = v;
    }

    std::vector<int> subCounts(archiveIds.size());
    for (auto &c : subCounts) c = readFormat();

    for (size_t i = 0; i < archiveIds.size(); i++)
    {
        int aid = archiveIds[i];
        auto &archive = archives[aid];
        auto &meta = meta_[aid];
        meta.subIds.resize(subCounts[i]);
        int fileId = 0;
        for (int j = 0; j < subCounts[i]; j++)
        {
            fileId += readFormat();
            archive.makeFile(fileId);
            meta.subIds[j] = fileId;
        }
    }

    if (hasNames)
    {
        for (size_t i = 0; i < archiveIds.size(); i++)
        {
            int aid = archiveIds[i];
            auto &archive = archives[aid];
            auto &meta = meta_[aid];
            meta.subNameHashes.resize(subCounts[i]);
            for (int j = 0; j < subCounts[i]; j++)
            {
                int nameHash = buffer.readInt();
                int fid = meta.subIds[j];
                archive.file(fid).name = nameHash;
                meta.subNameHashes[j] = nameHash;
            }
        }
    }
}

Archive &Js5Index::archive(int archiveId)
{
    auto it = archives.find(archiveId);
    if (it == archives.end())
    {
        archives[archiveId] = Archive();
        return archives[archiveId];
    }
    auto &archive = it->second;
    if (archive.loaded || archive.id == -1) return archive;

    auto raw = socket_->getFile(id, archiveId);
    auto decompressed = js5::decompress(raw.data(), raw.size());

    js5::unpackNetworkArchive(decompressed, meta_[archiveId].subIds, archive);
    archive.loaded = true;
    return archive;
}

}  // namespace js5
