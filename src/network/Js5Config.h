#pragma once

#include <string>

namespace js5 {

struct ServerConfig
{
    std::string key;            // 32-char client cache key from cnf.param
    int serverVersionMajor{};   // server_version (build number)
    int serverVersionMinor{1};
    std::string endpoint{"content.runescape.com"};
    int port{43594};
};

// Fetches https://world3.runescape.com/jav_config.ws?binaryType=2 and parses out
// the cache key + server version. Throws std::runtime_error on any failure.
ServerConfig fetchServerConfig();

}  // namespace js5
