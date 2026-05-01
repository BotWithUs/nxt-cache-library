#include "core/RSCache.h"

Index &RSCache::index(int id)
{
    auto it = indices.find(id);
    if (it != indices.end())
    {
        return it->second;
    }
    std::string filename = path + "\\js5-" + std::to_string(id) + ".jcache";
    indices.try_emplace(id, id, filename);
    return indices.at(id);
}

FileHeader &RSCache::file(int indexId, int archiveId, int fileId)
{
    auto &idx = RSCache::index(indexId);
    auto &archive = idx.archive(archiveId);
    return archive.file(fileId);
}
