#include "core/RSCache.h"

#include "network/Js5Compression.h"
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <cstdio>
#include <stdexcept>

RSCache::~RSCache() = default;

namespace {

// Returns a fallback resolver wired to the given socket. Captures by raw pointer
// because the socket is owned by RSCache itself and outlives every Index it created.
Index::FallbackFn makeResolver(js5::Js5Socket *socket)
{
    return [socket](int indexId, int archiveId)
        -> std::optional<std::vector<uint8_t>> {
        try
        {
            return socket->getFile(indexId, archiveId);
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr,
                         "Js5 fetch failed for %d.%d: %s\n",
                         indexId, archiveId, e.what());
            return std::nullopt;
        }
    };
}

}  // namespace

void RSCache::enableLiveFallback(bool beta)
{
    if (fallbackEnabled_) return;

    liveConfig_ = std::make_unique<js5::ServerConfig>(
        beta ? js5::fetchServerConfigBeta() : js5::fetchServerConfig());
    liveSocket_ = std::make_unique<js5::Js5Socket>(*liveConfig_);
    fallbackEnabled_ = true;

    auto resolver = makeResolver(liveSocket_.get());
    for (auto &[id, idx] : indices)
    {
        idx.setFallback(resolver);
    }
}

Index &RSCache::index(int id)
{
    auto it = indices.find(id);
    if (it != indices.end())
    {
        return it->second;
    }
    std::string filename = path + "\\js5-" + std::to_string(id) + ".jcache";
    Index::FallbackFn fallback;
    if (fallbackEnabled_) fallback = makeResolver(liveSocket_.get());
    indices.try_emplace(id, id, filename, std::move(fallback));
    return indices.at(id);
}

FileHeader &RSCache::file(int indexId, int archiveId, int fileId)
{
    auto &idx = RSCache::index(indexId);
    auto &archive = idx.archive(archiveId);
    return archive.file(fileId);
}

std::vector<int> RSCache::archiveIds(int indexId)
{
    return index(indexId).archiveIds;
}

Archive &RSCache::archive(int indexId, int archiveId)
{
    return index(indexId).archive(archiveId);
}

std::vector<int> RSCache::fileIds(int indexId, int archiveId)
{
    // The reference table (decoded when the Index is opened) already declares
    // every archive's file ids — read them straight from `archives` so we never
    // pull or decompress the archive data blob.
    Index &idx = index(indexId);
    auto it = idx.archives.find(archiveId);
    if (it == idx.archives.end()) return {};
    const Archive &a = it->second;
    return std::vector<int>(a.fileIds.begin(), a.fileIds.end());
}

void RSCache::evictArchive(int indexId, int archiveId)
{
    auto it = indices.find(indexId);
    if (it == indices.end()) return;
    it->second.archives.erase(archiveId);
}
