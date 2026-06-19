#include "network/Js5Cache.h"

namespace js5 {

Js5Cache::Js5Cache(bool beta)
    : config_(beta ? fetchServerConfigBeta() : fetchServerConfig()),
      socket_(std::make_unique<Js5Socket>(config_))
{
}

Js5Index &Js5Cache::index(int id)
{
    auto it = indices_.find(id);
    if (it != indices_.end()) return *it->second;
    auto inserted = indices_.emplace(id, std::make_unique<Js5Index>(id, socket_.get()));
    return *inserted.first->second;
}

std::vector<int> Js5Cache::archiveIds(int indexId)
{
    return index(indexId).archiveIds;
}

Archive &Js5Cache::archive(int indexId, int archiveId)
{
    return index(indexId).archive(archiveId);
}

std::vector<int> Js5Cache::fileIds(int indexId, int archiveId)
{
    // Reference-table file ids are populated at index construction (no group
    // fetch); read them from `archives` rather than calling archive(), which
    // would download + decompress the archive group over the wire.
    Js5Index &idx = index(indexId);
    auto it = idx.archives.find(archiveId);
    if (it == idx.archives.end()) return {};
    const Archive &a = it->second;
    return std::vector<int>(a.fileIds.begin(), a.fileIds.end());
}

}  // namespace js5
