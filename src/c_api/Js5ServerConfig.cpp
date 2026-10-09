#define NXTCACHE_BUILDING

#include "c_api/Js5ServerConfig.h"

#include <cstddef>
#include <string>
#include <utility>

namespace nxt::capi {

namespace {

constexpr std::size_t kKeyLength = 32;

// Length of a NUL-terminated string, scanning at most `limit + 1` characters so
// an over-long or unterminated key is never read past what the check needs.
std::size_t boundedLength(const char *s, std::size_t limit)
{
    std::size_t n = 0;
    while (n <= limit && s[n] != '\0')
    {
        n++;
    }
    return n;
}

}  // namespace

bool toServerConfig(const nxt_js5_server_config *config, js5::ServerConfig &outConfig,
                    std::string &outError)
{
    if (config == nullptr)
    {
        outError = "null config";
        return false;
    }
    if (config->struct_size != sizeof(nxt_js5_server_config))
    {
        outError = "config struct_size " + std::to_string(config->struct_size) + " != " +
                   std::to_string(sizeof(nxt_js5_server_config));
        return false;
    }
    if (config->key == nullptr)
    {
        outError = "config key is null";
        return false;
    }
    const std::size_t keyLength = boundedLength(config->key, kKeyLength);
    if (keyLength != kKeyLength)
    {
        outError = "config key must be exactly 32 characters";
        return false;
    }
    if (config->build_major <= 0)
    {
        outError = "config build_major must be > 0, got " + std::to_string(config->build_major);
        return false;
    }
    if (config->build_minor < 0)
    {
        outError = "config build_minor must be >= 0, got " + std::to_string(config->build_minor);
        return false;
    }

    js5::ServerConfig converted;
    converted.key.assign(config->key, kKeyLength);
    converted.serverVersionMajor = config->build_major;
    converted.serverVersionMinor = config->build_minor;
    if (config->host != nullptr && config->host[0] != '\0')
    {
        converted.endpoint = config->host;
    }
    if (config->port != 0)
    {
        converted.port = config->port;
    }
    outConfig = std::move(converted);
    return true;
}

}  // namespace nxt::capi
