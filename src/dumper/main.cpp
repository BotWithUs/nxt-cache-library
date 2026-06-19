#include "config_types/InterfaceTypes.h"
#include "config_types/ModelType.h"
#include "config_types/SpriteType.h"
#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/CacheSource.h"
#include "core/DbRowProvider.h"
#include "core/Index.h"
#include "core/RSCache.h"
#include "core/TypeMappings.h"
#include "dumper/Json.h"
#include "network/Js5Cache.h"
#include "render/IconRenderer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

using nxt::TypeMapping;

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
        << "                      worldmap | dbrow | if | sprite | model | itemicon\n"
        << "                      (sprite/model emit metadata only; pixel/geometry\n"
        << "                       bulk data is exposed via the C ABI. itemicon\n"
        << "                       renders an item's inventory icon to a BMP.)\n"
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
        << "  --width <n>         itemicon: output width  (default 64)\n"
        << "  --height <n>        itemicon: output height (default 64)\n"
        << "  --ss <n>            itemicon: supersampling factor 1..8 (default 4)\n"
        << "  --bg <RRGGBB>       itemicon: composite over this hex colour\n"
        << "                      (default: transparent background)\n"
        << "  --out-dir <path>    For --type if only: write one\n"
        << "                      interface-<id>.json per archive into <path>.\n"
        << "                      Each interface is decoded, written, and evicted\n"
        << "                      from memory before the next is read — required\n"
        << "                      for bulk dumps (the in-memory array swallows tens\n"
        << "                      of GB on a full sweep).\n"
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

// Load a single config entry by its registered type name (mirrors the C-ABI
// decodeOne path). Returns false if the archive/file is absent.
template<typename T>
bool loadOne(CacheSource &cache, const char *name, int id, T &out)
{
    const auto &defs = nxt::typeDefaults();
    auto it = defs.find(name);
    if (it == defs.end()) return false;
    const TypeMapping &m = it->second;
    int aid = m.archiveId, file = id;
    if (aid == -1) { aid = id >> m.shift; file = id & ((1 << m.shift) - 1); }
    auto &archive = cache.archive(m.indexId, aid);
    if (archive.id == -1) return false;
    auto buffer = archive.readFile(file);
    if (buffer.buffer == nullptr || buffer.remaining() == 0) return false;
    out.id = id;
    out.decode(buffer);
    return true;
}

void bmpU16(std::ofstream &f, int v) { char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)}; f.write(b, 2); }
void bmpU32(std::ofstream &f, uint32_t v)
{
    char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
    f.write(b, 4);
}

// Write an RGBA buffer to a 32-bit BMP with a real alpha channel (transparent
// background preserved). Uses BITMAPV4HEADER + BI_BITFIELDS so GDI+/most
// viewers honour the alpha. BMP rows are bottom-up; pixels are BGRA.
bool writeBmp32(const std::string &path, const std::vector<uint8_t> &rgba, int w, int h)
{
    const uint32_t imageSize = static_cast<uint32_t>(w) * h * 4;
    const uint32_t offBits = 14 + 108;  // file header + BITMAPV4HEADER (108 bytes)

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write("BM", 2);
    bmpU32(f, offBits + imageSize); bmpU16(f, 0); bmpU16(f, 0); bmpU32(f, offBits);
    bmpU32(f, 108); bmpU32(f, static_cast<uint32_t>(w)); bmpU32(f, static_cast<uint32_t>(h));
    bmpU16(f, 1); bmpU16(f, 32); bmpU32(f, 3 /*BI_BITFIELDS*/); bmpU32(f, imageSize);
    bmpU32(f, 2835); bmpU32(f, 2835); bmpU32(f, 0); bmpU32(f, 0);
    bmpU32(f, 0x00FF0000); bmpU32(f, 0x0000FF00); bmpU32(f, 0x000000FF); bmpU32(f, 0xFF000000);
    bmpU32(f, 0x73524742 /*'sRGB'*/);
    for (int i = 0; i < 12; ++i) bmpU32(f, 0);  // endpoints (9) + gamma R/G/B (3)

    std::vector<char> row(static_cast<size_t>(w) * 4);
    for (int y = h - 1; y >= 0; --y)
    {
        for (int x = 0; x < w; ++x)
        {
            size_t si = (static_cast<size_t>(y) * w + x) * 4;
            row[static_cast<size_t>(x) * 4 + 0] = char(rgba[si + 2]);  // B
            row[static_cast<size_t>(x) * 4 + 1] = char(rgba[si + 1]);  // G
            row[static_cast<size_t>(x) * 4 + 2] = char(rgba[si + 0]);  // R
            row[static_cast<size_t>(x) * 4 + 3] = char(rgba[si + 3]);  // A
        }
        f.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(f);
}

// Write an RGBA buffer to a 24-bit BMP, compositing over a background colour
// (for when an opaque slot-style backdrop is wanted instead of transparency).
bool writeBmp24(const std::string &path, const std::vector<uint8_t> &rgba,
                int w, int h, uint8_t bgR, uint8_t bgG, uint8_t bgB)
{
    const int rowBytes = w * 3;
    const int pad = (4 - (rowBytes % 4)) % 4;
    const int imageSize = (rowBytes + pad) * h;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write("BM", 2);
    bmpU32(f, 54 + imageSize); bmpU16(f, 0); bmpU16(f, 0); bmpU32(f, 54);
    bmpU32(f, 40); bmpU32(f, static_cast<uint32_t>(w)); bmpU32(f, static_cast<uint32_t>(h));
    bmpU16(f, 1); bmpU16(f, 24);
    bmpU32(f, 0); bmpU32(f, static_cast<uint32_t>(imageSize)); bmpU32(f, 2835); bmpU32(f, 2835);
    bmpU32(f, 0); bmpU32(f, 0);

    std::vector<char> row(static_cast<size_t>(rowBytes + pad), 0);
    for (int y = h - 1; y >= 0; --y)
    {
        for (int x = 0; x < w; ++x)
        {
            size_t si = (static_cast<size_t>(y) * w + x) * 4;
            float a = rgba[si + 3] / 255.0f;
            int r = static_cast<int>(rgba[si + 0] * a + bgR * (1.0f - a) + 0.5f);
            int g = static_cast<int>(rgba[si + 1] * a + bgG * (1.0f - a) + 0.5f);
            int b = static_cast<int>(rgba[si + 2] * a + bgB * (1.0f - a) + 0.5f);
            row[static_cast<size_t>(x) * 3 + 0] = char(b);  // BMP is BGR
            row[static_cast<size_t>(x) * 3 + 1] = char(g);
            row[static_cast<size_t>(x) * 3 + 2] = char(r);
        }
        f.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(f);
}

} // namespace

int main(int argc, char **argv)
{
    std::string cachePath;
    std::string typeName;
    std::string outPath;
    std::string outDir;
    std::string sourceMode = "local";
    std::string fallbackMode;
    std::optional<int> id;
    std::optional<int> limit;
    std::optional<int> indexOverride;
    std::optional<int> archiveOverride;
    std::optional<int> shiftOverride;
    bool pretty = false;
    int iconW = 64, iconH = 64, iconSS = 4;   // --type itemicon output size + SSAA
    std::string iconBg;                       // itemicon background: "" = transparent, else RRGGBB
    std::optional<int> ovXan, ovYan, ovZan, ovZoom;  // itemicon rotation/zoom overrides (debug)

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
        else if (a == "--out-dir")  outDir          = takeArg(i, "--out-dir");
        else if (a == "--width")    iconW           = takeIntArg(i, "--width");
        else if (a == "--height")   iconH           = takeIntArg(i, "--height");
        else if (a == "--ss")       iconSS          = takeIntArg(i, "--ss");
        else if (a == "--bg")       iconBg          = takeArg(i, "--bg");
        else if (a == "--xan")      ovXan           = takeIntArg(i, "--xan");
        else if (a == "--yan")      ovYan           = takeIntArg(i, "--yan");
        else if (a == "--zan")      ovZan           = takeIntArg(i, "--zan");
        else if (a == "--zoom")     ovZoom          = takeIntArg(i, "--zoom");
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

    const auto &kDefaults = nxt::typeDefaults();
    // "itemicon" is a render mode, not a config type, so it has no TypeMapping.
    const bool isItemIcon = (typeName == "itemicon");
    TypeMapping m{};
    if (!isItemIcon)
    {
        auto it = kDefaults.find(typeName);
        if (it == kDefaults.end())
        {
            std::cerr << "Unknown --type '" << typeName << "'\n\n";
            usage(argv[0]);
            return 2;
        }
        m = it->second;
        if (indexOverride)   m.indexId = *indexOverride;
        if (archiveOverride) m.archiveId = *archiveOverride;
        if (shiftOverride)   m.shift = *shiftOverride;
    }

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

    // Render mode: --type itemicon --id <item> [--out file.bmp] [--width/--height/--ss].
    // Renders the item's inventory icon to a 24-bit BMP (composited over slot grey).
    if (isItemIcon)
    {
        if (!id)
        {
            std::cerr << "--type itemicon requires --id <item>\n";
            return 2;
        }
        std::string bmpPath = outPath.empty()
                                  ? ("itemicon-" + std::to_string(*id) + ".bmp")
                                  : outPath;
        try
        {
            ItemType item;
            if (!loadOne(*cache, "item", *id, item))
            {
                std::cerr << "item " << *id << " not found\n";
                return 1;
            }
            if (item.modelID <= 0)
            {
                std::cerr << "item " << *id << " ('" << item.name << "') has no inventory model\n";
                return 1;
            }
            ModelType model;
            if (!loadOne(*cache, "model", item.modelID, model))
            {
                std::cerr << "inventory model " << item.modelID << " not found\n";
                return 1;
            }
            nxtrender::IconParams p;
            p.zoom2d    = item.modelZoom;
            p.xan2d     = item.modelRotationX;
            p.yan2d     = item.modelRotationY;
            p.zan2d     = item.modelAngleZ;
            p.offsetX2d = item.modelOffsetX;
            p.offsetY2d = item.modelOffsetY;
            p.resizeX   = item.resizeX;
            p.resizeY   = item.resizeY;
            p.resizeZ   = item.resizeZ;
            p.ambient   = item.ambient;
            p.contrast  = item.contrast;
            p.origColors = item.originalColors;
            p.replColors = item.replacementColors;
            p.supersample = iconSS <= 0 ? 4 : iconSS;
            if (ovXan)  p.xan2d = *ovXan;
            if (ovYan)  p.yan2d = *ovYan;
            if (ovZan)  p.zan2d = *ovZan;
            if (ovZoom) p.zoom2d = *ovZoom;
            std::cerr << "  params: zoom=" << p.zoom2d << " xan=" << p.xan2d
                      << " yan=" << p.yan2d << " zan=" << p.zan2d
                      << " offX=" << p.offsetX2d << " offY=" << p.offsetY2d << "\n";

            auto icon = nxtrender::renderModelIcon(model, p, iconW, iconH);
            bool ok;
            if (iconBg.empty())
            {
                // Transparent background (32-bit BMP with alpha).
                ok = writeBmp32(bmpPath, icon.rgba, icon.width, icon.height);
            }
            else
            {
                // Composite over the given RRGGBB hex colour (24-bit BMP).
                unsigned bg = std::stoul(iconBg, nullptr, 16);
                ok = writeBmp24(bmpPath, icon.rgba, icon.width, icon.height,
                                static_cast<uint8_t>((bg >> 16) & 0xFF),
                                static_cast<uint8_t>((bg >> 8) & 0xFF),
                                static_cast<uint8_t>(bg & 0xFF));
            }
            if (!ok)
            {
                std::cerr << "failed to write " << bmpPath << "\n";
                return 1;
            }
            std::cerr << "Wrote " << bmpPath << " (" << icon.width << "x" << icon.height
                      << ", item '" << item.name << "', model " << item.modelID << ")\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << "itemicon render failed: " << e.what() << "\n";
            return 1;
        }
    }

    // Streaming path: --type if --out-dir <path>. Write one
    // interface-<id>.json per archive and evict the archive between files so
    // peak RSS stays bounded by the largest single interface.
    if (typeName == "if" && !outDir.empty())
    {
        if (id || limit || indexOverride || archiveOverride || shiftOverride)
        {
            std::cerr << "--out-dir is the bulk-sweep mode; combine with no other selectors\n";
            return 2;
        }
        try
        {
            std::filesystem::create_directories(outDir);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Cannot create --out-dir '" << outDir << "': " << e.what() << "\n";
            return 1;
        }

        try
        {
            std::vector<int> aids = cache->archiveIds(m.indexId);
            int written = 0, skipped = 0;
            for (int aid : aids)
            {
                auto &archive = cache->archive(m.indexId, aid);
                if (archive.id == -1) { skipped++; continue; }

                InterfaceDef def;
                def.id = aid;
                for (auto &[fid, fh] : archive.files)
                {
                    auto buffer = archive.readFile(fid);
                    if (buffer.buffer == nullptr || buffer.remaining() == 0) continue;
                    InterfaceComponentDef comp;
                    comp.id = fid;
                    comp.decode(buffer);
                    def.components.emplace(fid, std::move(comp));
                }
                if (def.components.empty())
                {
                    cache->evictArchive(m.indexId, aid);
                    skipped++;
                    continue;
                }

                json doc = nxtdump::toJson(def);
                std::string s = pretty ? doc.dump(2) : doc.dump();
                std::filesystem::path file =
                    std::filesystem::path(outDir) / ("interface-" + std::to_string(aid) + ".json");
                std::ofstream f(file, std::ios::binary);
                if (!f)
                {
                    std::cerr << "Failed to open " << file << "\n";
                    return 1;
                }
                f << s;
                f.close();

                cache->evictArchive(m.indexId, aid);
                written++;
                if (written % 50 == 0)
                {
                    std::cerr << "  written " << written << "/" << aids.size() << "\n";
                }
            }
            std::cerr << "Done: " << written << " written, " << skipped << " empty/missing.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Stream-dump failed: " << e.what() << "\n";
            return 1;
        }
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
        else if (typeName == "sprite")   out = dumpRange<SpriteType>(*cache, m, id, limit);
        else if (typeName == "model")    out = dumpRange<ModelType>(*cache, m, id, limit);
        else if (typeName == "dbrow")    out = dumpDbRows(*cache, m, id, limit);
        else if (typeName == "if")
        {
            // Per-archive sweep: one archive = one interface = N components.
            out = json::array();
            int count = 0;
            auto wantMore = [&]() { return !limit || count < *limit; };
            auto dumpOne = [&](int aid) -> bool {
                auto &archive = cache->archive(m.indexId, aid);
                if (archive.id == -1) return false;
                InterfaceDef def;
                def.id = aid;
                for (auto &[fid, fh] : archive.files)
                {
                    auto buffer = archive.readFile(fid);
                    if (buffer.buffer == nullptr || buffer.remaining() == 0) continue;
                    InterfaceComponentDef comp;
                    comp.id = fid;
                    comp.decode(buffer);
                    def.components.emplace(fid, std::move(comp));
                }
                if (def.components.empty()) return false;
                out.push_back(nxtdump::toJson(def));
                count++;
                return true;
            };
            if (id)
            {
                dumpOne(*id);
            }
            else
            {
                std::vector<int> aids = cache->archiveIds(m.indexId);
                for (int aid : aids)
                {
                    if (!wantMore()) break;
                    dumpOne(aid);
                }
            }
        }
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
