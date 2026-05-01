#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/CacheSource.h"
#include "core/DbRowProvider.h"
#include "core/Index.h"
#include "core/RSCache.h"
#include "dumper/Json.h"
#include "network/Js5Cache.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace {

struct TypeMapping
{
    std::string name;
    int indexId;
    int archiveId;  // -1 indicates sharded layout
    int shift;
};

// Defaults match the common NXT/RS3 cache layout. Override with --index/--archive/--shift
// when targeting a different revision.
const std::map<std::string, TypeMapping> kDefaults = {
    {"npc",      {"npc",      18, -1,  7}},
    {"item",     {"item",     19, -1,  8}},
    {"loc",      {"loc",      16, -1,  8}},
    {"seq",      {"seq",      20, -1,  7}},
    {"varbit",   {"varbit",   22, -1, 10}},
    {"enum",     {"enum",     17, -1,  8}},
    {"struct",   {"struct",   26, -1, 10}},
    {"inv",      {"inv",       2,  5,  0}},
    {"param",    {"param",     2, 11,  0}},
    {"quest",    {"quest",     2, 35,  0}},
    {"underlay", {"underlay",  2,  1,  0}},
    {"overlay",  {"overlay",   2,  4,  0}},
    {"worldmap", {"worldmap", 23,  0,  0}},
    {"dbrow",    {"dbrow",     2, 41,  0}},
};

void usage(const char *prog)
{
    std::cerr
        << "NXTCache JSON dumper\n"
        << "\n"
        << "Usage: " << prog << " --type <name> [--cache <path> | --source live] [options]\n"
        << "\n"
        << "Required:\n"
        << "  --type <name>       npc | item | loc | seq | varbit | enum | struct |\n"
        << "                      inv | param | quest | underlay | overlay |\n"
        << "                      worldmap | dbrow\n"
        << "\n"
        << "Source (pick one; default: local):\n"
        << "  --cache <path>      Read from local sqlite jcache (js5-*.jcache files)\n"
        << "  --source live       Fetch directly from Jagex JS5 servers\n"
        << "  --source local      (default) same as --cache\n"
        << "  --fallback live     With --cache, transparently pull missing archives\n"
        << "                      from the live JS5 source\n"
        << "\n"
        << "Optional:\n"
        << "  --id <n>            Dump a single id (default: dump all known)\n"
        << "  --limit <n>         Maximum number of entries to dump\n"
        << "  --index <n>         Override default JS5 index id\n"
        << "  --archive <n>       Override default archive id (-1 = sharded)\n"
        << "  --shift <n>         Override default shift (sharded layout)\n"
        << "  --out <file>        Write output to file instead of stdout\n"
        << "  --pretty            Pretty-print JSON (indent=2)\n"
        << "  --help              Show this message\n";
}

template<typename T>
json decodeFile(Archive &archive, int fileId, int actualId)
{
    auto buffer = archive.readFile(fileId);
    if (buffer.buffer == nullptr || buffer.remaining() == 0) return nullptr;
    T type{};
    type.id = actualId;
    type.decode(buffer);
    return nxtdump::toJson(type);
}

template<typename T>
json dumpRange(CacheSource &cache, const TypeMapping &m,
               std::optional<int> singleId, std::optional<int> limit)
{
    json out = json::array();

    if (singleId)
    {
        int id = *singleId;
        int aid = m.archiveId;
        int file = id;
        if (aid == -1)
        {
            aid = id >> m.shift;
            file = id & ((1 << m.shift) - 1);
        }
        auto &archive = cache.archive(m.indexId, aid);
        if (archive.id == -1) return out;
        json entry = decodeFile<T>(archive, file, id);
        if (!entry.is_null()) out.push_back(std::move(entry));
        return out;
    }

    int count = 0;
    auto wantMore = [&]() { return !limit || count < *limit; };

    if (m.archiveId == -1)
    {
        std::vector<int> aids = cache.archiveIds(m.indexId);
        for (int aid: aids)
        {
            if (!wantMore()) break;
            auto &archive = cache.archive(m.indexId, aid);
            if (archive.id == -1) continue;
            for (auto &[fid, fh]: archive.files)
            {
                if (!wantMore()) break;
                int actualId = (aid << m.shift) | fid;
                json entry = decodeFile<T>(archive, fid, actualId);
                if (!entry.is_null())
                {
                    out.push_back(std::move(entry));
                    count++;
                }
            }
        }
    }
    else
    {
        auto &archive = cache.archive(m.indexId, m.archiveId);
        if (archive.id != -1)
        {
            for (auto &[fid, fh]: archive.files)
            {
                if (!wantMore()) break;
                json entry = decodeFile<T>(archive, fid, fid);
                if (!entry.is_null())
                {
                    out.push_back(std::move(entry));
                    count++;
                }
            }
        }
    }
    return out;
}

json dumpDbRows(CacheSource &cache, const TypeMapping &m,
                std::optional<int> singleId, std::optional<int> limit)
{
    json out = json::array();
    DbRowProvider provider(&cache);
    provider.load();

    auto &archive = cache.archive(m.indexId, m.archiveId);
    if (archive.id == -1) return out;

    int count = 0;
    for (auto &[fid, fh]: archive.files)
    {
        if (limit && count >= *limit) break;
        if (singleId && fid != *singleId) continue;
        DbRowType *row = provider.get(fid);
        if (row && row->id != -1)
        {
            out.push_back(nxtdump::toJson(*row));
            count++;
        }
    }
    return out;
}

bool parseInt(const std::string &s, int &out)
{
    try
    {
        size_t end = 0;
        int v = std::stoi(s, &end, 0);
        if (end != s.size()) return false;
        out = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

int main(int argc, char **argv)
{
    std::string cachePath;
    std::string typeName;
    std::string outPath;
    std::string sourceMode = "local";
    std::string fallbackMode;
    std::optional<int> id;
    std::optional<int> limit;
    std::optional<int> indexOverride;
    std::optional<int> archiveOverride;
    std::optional<int> shiftOverride;
    bool pretty = false;

    auto takeArg = [&](int &i, const char *what) -> std::string {
        if (i + 1 >= argc)
        {
            std::cerr << "Missing argument after " << what << "\n";
            std::exit(2);
        }
        return argv[++i];
    };

    auto takeIntArg = [&](int &i, const char *what) -> int {
        std::string s = takeArg(i, what);
        int v = 0;
        if (!parseInt(s, v))
        {
            std::cerr << "Invalid integer after " << what << ": " << s << "\n";
            std::exit(2);
        }
        return v;
    };

    for (int i = 1; i < argc; i++)
    {
        std::string_view a = argv[i];
        if (a == "--help" || a == "-h")
        {
            usage(argv[0]);
            return 0;
        }
        else if (a == "--cache")    cachePath = takeArg(i, "--cache");
        else if (a == "--source")   sourceMode = takeArg(i, "--source");
        else if (a == "--fallback") fallbackMode = takeArg(i, "--fallback");
        else if (a == "--type")     typeName  = takeArg(i, "--type");
        else if (a == "--id")       id              = takeIntArg(i, "--id");
        else if (a == "--limit")    limit           = takeIntArg(i, "--limit");
        else if (a == "--index")    indexOverride   = takeIntArg(i, "--index");
        else if (a == "--archive")  archiveOverride = takeIntArg(i, "--archive");
        else if (a == "--shift")    shiftOverride   = takeIntArg(i, "--shift");
        else if (a == "--out")      outPath         = takeArg(i, "--out");
        else if (a == "--pretty")   pretty          = true;
        else
        {
            std::cerr << "Unknown argument: " << a << "\n\n";
            usage(argv[0]);
            return 2;
        }
    }

    if (typeName.empty())
    {
        usage(argv[0]);
        return 2;
    }
    if (sourceMode != "local" && sourceMode != "live")
    {
        std::cerr << "Invalid --source '" << sourceMode << "' (expected: local | live)\n\n";
        usage(argv[0]);
        return 2;
    }
    if (sourceMode == "local" && cachePath.empty())
    {
        std::cerr << "--cache <path> is required when --source local (default)\n\n";
        usage(argv[0]);
        return 2;
    }

    auto it = kDefaults.find(typeName);
    if (it == kDefaults.end())
    {
        std::cerr << "Unknown --type '" << typeName << "'\n\n";
        usage(argv[0]);
        return 2;
    }
    TypeMapping m = it->second;
    if (indexOverride)   m.indexId = *indexOverride;
    if (archiveOverride) m.archiveId = *archiveOverride;
    if (shiftOverride)   m.shift = *shiftOverride;

    if (!fallbackMode.empty() && fallbackMode != "live")
    {
        std::cerr << "Invalid --fallback '" << fallbackMode << "' (only 'live' is supported)\n\n";
        usage(argv[0]);
        return 2;
    }
    if (!fallbackMode.empty() && sourceMode == "live")
    {
        std::cerr << "--fallback live is redundant when --source live is set\n\n";
        return 2;
    }

    std::unique_ptr<CacheSource> cache;
    try
    {
        if (sourceMode == "live")
        {
            auto live = std::make_unique<js5::Js5Cache>();
            std::cerr << "Connected to live JS5 (build "
                      << live->serverConfig().serverVersionMajor << ")\n";
            cache = std::move(live);
        }
        else
        {
            auto local = std::make_unique<RSCache>(cachePath);
            if (fallbackMode == "live")
            {
                local->enableLiveFallback();
                std::cerr << "Live JS5 fallback enabled\n";
            }
            cache = std::move(local);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to open cache source: " << e.what() << "\n";
        return 1;
    }

    json out;

    try
    {
        if (typeName == "npc")           out = dumpRange<NpcType>(*cache, m, id, limit);
        else if (typeName == "item")     out = dumpRange<ItemType>(*cache, m, id, limit);
        else if (typeName == "loc")      out = dumpRange<LocationType>(*cache, m, id, limit);
        else if (typeName == "seq")      out = dumpRange<SequenceType>(*cache, m, id, limit);
        else if (typeName == "varbit")   out = dumpRange<VarbitType>(*cache, m, id, limit);
        else if (typeName == "enum")     out = dumpRange<EnumType>(*cache, m, id, limit);
        else if (typeName == "struct")   out = dumpRange<StructType>(*cache, m, id, limit);
        else if (typeName == "inv")      out = dumpRange<InventoryType>(*cache, m, id, limit);
        else if (typeName == "param")    out = dumpRange<ParamType>(*cache, m, id, limit);
        else if (typeName == "quest")    out = dumpRange<QuestType>(*cache, m, id, limit);
        else if (typeName == "underlay") out = dumpRange<UnderlayType>(*cache, m, id, limit);
        else if (typeName == "overlay")  out = dumpRange<OverlayType>(*cache, m, id, limit);
        else if (typeName == "worldmap") out = dumpRange<WorldMapElementType>(*cache, m, id, limit);
        else if (typeName == "dbrow")    out = dumpDbRows(*cache, m, id, limit);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Decode error: " << e.what() << "\n";
        return 1;
    }

    json envelope = {
        {"type", typeName},
        {"index", m.indexId},
        {"archive", m.archiveId},
        {"shift", m.shift},
        {"count", out.size()},
        {"entries", std::move(out)},
    };

    std::string serialized = pretty ? envelope.dump(2) : envelope.dump();

    if (outPath.empty())
    {
        std::cout << serialized << std::endl;
    }
    else
    {
        std::ofstream f(outPath, std::ios::binary);
        if (!f)
        {
            std::cerr << "Failed to open output file: " << outPath << "\n";
            return 1;
        }
        f << serialized;
    }

    return 0;
}
