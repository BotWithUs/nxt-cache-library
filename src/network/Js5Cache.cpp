#include "network/Js5Cache.h"

namespace js5 {

Js5Cache::Js5Cache()
    : config_(fetchServerConfig()),
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

}  // namespace js5
