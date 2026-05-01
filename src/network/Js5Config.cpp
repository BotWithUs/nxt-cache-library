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

}  // namespace

ServerConfig fetchServerConfig()
{
    std::string body = httpGet("world3.runescape.com",
                                "/jav_config.ws?binaryType=2",
                                /*useTls=*/false);
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
    for (const auto &[k, v] : paramIt->second)
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
    return out;
}

}  // namespace js5
