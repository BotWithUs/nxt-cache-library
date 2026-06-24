#include "network/Js5Config.h"

#include "network/HttpClient.h"

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace js5 {

namespace {

// jav_config returns key=value lines, optionally key=subkey=value (e.g. "param=key=value").
// We store top-level key=value pairs in `flat` and key=subkey=value triples in `nested`.
struct ParsedConfig
{
    std::map<std::string, std::string> flat;
    std::map<std::string, std::map<std::string, std::string>> nested;
};

ParsedConfig parseLines(const std::string &body)
{
    ParsedConfig out;
    std::stringstream ss(body);
    std::string line;
    while (std::getline(ss, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t eq1 = line.find('=');
        if (eq1 == std::string::npos) continue;
        std::string k1 = line.substr(0, eq1);
        std::string rest = line.substr(eq1 + 1);

        size_t eq2 = rest.find('=');
        if (eq2 == std::string::npos)
        {
            out.flat[k1] = rest;
        }
        else
        {
            std::string k2 = rest.substr(0, eq2);
            std::string v = rest.substr(eq2 + 1);
            out.nested[k1][k2] = v;
        }
    }
    return out;
}

ServerConfig parseConfig(const std::string &body)
{
    ParsedConfig cfg = parseLines(body);

    ServerConfig out;

    auto svIt = cfg.flat.find("server_version");
    if (svIt == cfg.flat.end())
    {
        throw std::runtime_error("jav_config: missing server_version");
    }
    out.serverVersionMajor = std::stoi(svIt->second);

    auto paramIt = cfg.nested.find("param");
    if (paramIt == cfg.nested.end())
    {
        throw std::runtime_error("jav_config: missing param block");
    }
    const auto &params = paramIt->second;
    for (const auto &[k, v] : params)
    {
        if (v.size() == 32)
        {
            out.key = v;
            break;
        }
    }
    if (out.key.empty())
    {
        throw std::runtime_error("jav_config: 32-char cache key not found in param");
    }

    // The JS5 content host is advertised in the param block (param=37, with
    // param=49 as a fallback). The live and beta configs point at different
    // hosts (content.runescape.com vs content.beta.runescape.com), so we must
    // honour what the config says rather than assume the default endpoint.
    auto hostIt = params.find("37");
    if (hostIt == params.end())
    {
        hostIt = params.find("49");
    }
    if (hostIt != params.end() && !hostIt->second.empty())
    {
        out.endpoint = hostIt->second;
    }
    return out;
}

}  // namespace

ServerConfig fetchServerConfig()
{
    std::string body = httpGet("world3.runescape.com",
                                "/jav_config.ws?binaryType=2",
                                /*useTls=*/false);
    return parseConfig(body);
}

ServerConfig fetchServerConfigBeta()
{
    // The gameval/Lua beta (which carries cache index 67) is served from
    // www.runescape.com's jav_config_beta — NOT world1's, which tracks a
    // near-live build (currently 948) on content.runescape.com and has no
    // index 67. www's beta config reports the real beta build (currently 947)
    // and points param=37 at content.beta.runescape.com. The 32-char cache key
    // rotates on every request, so this single fetch must feed the handshake
    // directly (no caching between fetch and connect).
    std::string body = httpGet("www.runescape.com",
                                "/jav_config_beta.ws?binaryType=3",
                                /*useTls=*/true);
    return parseConfig(body);
}

}  // namespace js5
