#pragma once

#include "c_api/nxtcache_c.h"
#include "network/Js5Config.h"

#include <string>

namespace nxt::capi {

// Validates a caller-supplied nxt_js5_server_config and converts it into the
// js5::ServerConfig the network layer takes. Applies the documented defaults
// (NULL/"" host -> content.runescape.com, port 0 -> 43594) and leaves the
// timeouts at the ServerConfig defaults. On failure returns false, sets
// outError to a message that never contains the key, and leaves outConfig
// untouched.
bool toServerConfig(const nxt_js5_server_config *config, js5::ServerConfig &outConfig,
                    std::string &outError);

}  // namespace nxt::capi
