#pragma once

#include "core/CacheSource.h"
#include "network/Js5Config.h"
#include "network/Js5Index.h"
#include "network/Js5Socket.h"

#include <map>
#include <memory>

namespace js5 {

// CacheSource backed by the live JS5 binary protocol. Construction performs the
// jav_config.ws fetch and the TCP handshake; per-major reference tables are loaded
// lazily on first index() call, archives lazily on first archive() call.
class Js5Cache : public CacheSource
{
public:
    // beta=true hits the BETA jav_config + JS5 endpoint instead of live.
    explicit Js5Cache(bool beta = false);

    Js5Index &index(int id);

    std::vector<int> archiveIds(int indexId) override;
    Archive &archive(int indexId, int archiveId) override;

    const ServerConfig &serverConfig() const { return config_; }

private:
    ServerConfig config_;
    std::unique_ptr<Js5Socket> socket_;
    std::map<int, std::unique_ptr<Js5Index>> indices_;
};

}  // namespace js5
