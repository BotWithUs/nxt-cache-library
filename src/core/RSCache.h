#pragma once

#include "core/CacheSource.h"
#include "core/Index.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace js5 {
class Js5Socket;
struct ServerConfig;
}

class RSCache : public CacheSource
{
    std::map<int, Index> indices;
    std::unique_ptr<js5::ServerConfig> liveConfig_;
    std::unique_ptr<js5::Js5Socket> liveSocket_;
    bool fallbackEnabled_ = false;
public:
    std::string path;

    explicit RSCache(std::string path) : path(std::move(path)) {}
    ~RSCache();

    Index &index(int id);

    FileHeader &file(int indexId, int archiveId, int fileId);

    std::vector<int> archiveIds(int indexId) override;
    Archive &archive(int indexId, int archiveId) override;
    std::vector<int> fileIds(int indexId, int archiveId) override;
    void evictArchive(int indexId, int archiveId) override;

    // Opens a connection to the live JS5 servers and installs a fallback
    // resolver on every Index in this cache (existing and future). Subsequent
    // reads transparently fetch from network when the local sqlite has no blob.
    // Throws std::runtime_error if the jav_config fetch or handshake fails.
    // Pass beta=true to hit the BETA jav_config + JS5 endpoint instead of live.
    void enableLiveFallback(bool beta = false);
};
