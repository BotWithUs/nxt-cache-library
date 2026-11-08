#pragma once

#include "core/CacheSource.h"

#include <optional>
#include <string>
#include <vector>

namespace nxtdump {

struct ClientScriptDumpOptions
{
    std::optional<int> id;           // one script, else every script in index 12
    std::optional<int> limit;        // cap on scripts visited
    std::string opcodesPath;         // opcodes.json; empty = raw numbers only
    std::vector<int> grepOps;        // non-empty: list scripts using any of these
    bool isJson{false};              // JSON instead of assembly text
    bool isPretty{false};
    std::string outPath;             // single output file; empty = stdout
    std::string outDir;              // one file per script
};

// --type clientscript. Returns the process exit code.
int runClientScriptDump(CacheSource &cache, const ClientScriptDumpOptions &options);

}  // namespace nxtdump
