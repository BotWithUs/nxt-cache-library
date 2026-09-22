#pragma once

#include <string>

namespace js5 {

constexpr int kDefaultConnectTimeoutMs = 10000;
constexpr int kDefaultIoTimeoutMs = 30000;

struct ServerConfig
{
    std::string key;            // 32-char client cache key from cnf.param
    int serverVersionMajor{};   // server_version (build number)
    int serverVersionMinor{1};
    std::string endpoint{"content.runescape.com"};
    int port{43594};
    // Bounds on blocking network steps (<= 0 = OS default, i.e. unbounded).
    // connectTimeoutMs caps the TCP connect; ioTimeoutMs caps any single
    // recv/send that makes no progress, so a stalled server surfaces as an
    // exception (NXT_ERR_IO at the C ABI) instead of a hang.
    int connectTimeoutMs{kDefaultConnectTimeoutMs};
    int ioTimeoutMs{kDefaultIoTimeoutMs};
};

// Fetches https://world3.runescape.com/jav_config.ws?binaryType=2 and parses out
// the cache key + server version. Throws std::runtime_error on any failure.
ServerConfig fetchServerConfig();

// Beta variant — fetches https://www.runescape.com/jav_config_beta.ws?binaryType=3,
// the gameval/Lua beta that carries cache index 67 (build 947 on
// content.beta.runescape.com). Beta runs a different server_version + cache key
// and a different content host, all parsed from its own jav_config.
ServerConfig fetchServerConfigBeta();

}  // namespace js5
