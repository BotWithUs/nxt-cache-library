// Tests for the world map element placement export (cache index 41).
//
//   nxtcache-worldmap-elements-test [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache (or its index 41)
// absent (SKIP). The decoder and argument checks that need no cache run first.
//
// Against a real cache the placements are checked against data that does not
// come from index 41:
//  - every placed element id must resolve to a WME definition (index 2/36);
//  - the Lumbridge, Varrock and Falador lodestone locs, found in their map
//    squares (index 5), must each have a placement of a HOME_TELEPORT_DESTINATION
//    element within 2 tiles, and the town's name label must sit near it.
// The lodestone loc ids and category id below are cache data of this revision
// (951); if Jagex renumbers them this test fails loudly rather than passing.

#include "c_api/nxtcache_c.h"
#include "maps/WorldMapElements.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kWmeIndex = 41;
constexpr int kHomeTeleportCategory = 2613;   // category gameval HOME_TELEPORT_DESTINATION
constexpr int kLodestoneTolerance = 2;        // tiles, Chebyshev
constexpr int kLabelTolerance = 96;        // town labels sit mid-town, not at the lodestone

int failures = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

std::string cachePathFrom(int argc, char **argv)
{
    if (argc > 1)
    {
        return argv[1];
    }
    if (const char *env = std::getenv("NXTCACHE_TEST_CACHE"))
    {
        return env;
    }
    return R"(C:\ProgramData\Jagex\RuneScape)";
}

std::vector<uint8_t> recordBytes(uint32_t packed, uint16_t element, uint8_t flags)
{
    return {uint8_t(packed >> 24), uint8_t(packed >> 16), uint8_t(packed >> 8), uint8_t(packed),
            uint8_t(element >> 8),  uint8_t(element),      flags};
}

void testDecoder()
{
    std::printf("Decoder (no cache)\n");
    // Two records: (3233, 3222, plane 0) element 3612, and (3211, 3220, plane 1)
    // element 1477 with flags 1.
    const uint32_t lumbridge = (3233u << 14) | 3222u;
    const uint32_t duke = (1u << 28) | (3211u << 14) | 3220u;
    std::vector<uint8_t> file = {0x00, 0x02};
    for (const auto &r : {recordBytes(lumbridge, 3612, 0), recordBytes(duke, 1477, 1)})
    {
        file.insert(file.end(), r.begin(), r.end());
    }

    std::vector<maps::WorldMapElementPlacement> out;
    const bool isOk = maps::decodeWorldMapElementFile(file.data(), file.size(), 28, out);
    check(isOk && out.size() == 2, "two-record file decodes to two placements");
    if (isOk && out.size() == 2)
    {
        const auto &a = out[0];
        const auto &b = out[1];
        check(a.areaId == 28 && a.elementId == 3612 && a.worldX == 3233 && a.worldY == 3222
                  && a.plane == 0 && a.flags == 0,
              "record 0 fields");
        check(b.elementId == 1477 && b.worldX == 3211 && b.worldY == 3220 && b.plane == 1
                  && b.flags == 1,
              "record 1 fields (plane bits, flags byte)");
    }

    std::vector<uint8_t> shortFile(file.begin(), file.end() - 1);
    check(!maps::decodeWorldMapElementFile(shortFile.data(), shortFile.size(), 28, out) && out.empty(),
          "one byte short -> rejected, output cleared");
    std::vector<uint8_t> longFile = file;
    longFile.push_back(0);
    check(!maps::decodeWorldMapElementFile(longFile.data(), longFile.size(), 28, out) && out.empty(),
          "one trailing byte -> rejected, output cleared");
    const uint8_t empty[2] = {0, 0};
    check(maps::decodeWorldMapElementFile(empty, 2, 28, out) && out.empty(),
          "count 0 -> accepted, no placements");
    check(!maps::decodeWorldMapElementFile(empty, 1, 28, out), "1-byte file -> rejected");

    nxt_wme_placement *placements = nullptr;
    size_t count = 0;
    check(nxt_get_worldmap_elements(nullptr, &placements, &count) == NXT_ERR_INVALID,
          "nxt_get_worldmap_elements null handle -> NXT_ERR_INVALID");
    check(nxt_get_worldmap_area_elements(nullptr, 28, &placements, &count) == NXT_ERR_INVALID,
          "nxt_get_worldmap_area_elements null handle -> NXT_ERR_INVALID");
}

struct WmeDef
{
    int category;
    std::string name;
};

struct Fetched
{
    nxt_result rc{};
    bool isOutCleared{};
    std::vector<nxt_wme_placement> placements;
};

Fetched fetchArea(nxt_cache *cache, int areaId)
{
    auto *buf = reinterpret_cast<nxt_wme_placement *>(static_cast<uintptr_t>(0x1));
    size_t count = 12345;
    Fetched f;
    f.rc = nxt_get_worldmap_area_elements(cache, areaId, &buf, &count);
    f.isOutCleared = buf == nullptr && count == 0;
    if (f.rc == NXT_OK && buf != nullptr)
    {
        f.placements.assign(buf, buf + count);
        nxt_free(buf);
    }
    return f;
}

Fetched fetchAll(nxt_cache *cache)
{
    nxt_wme_placement *buf = nullptr;
    size_t count = 0;
    Fetched f;
    f.rc = nxt_get_worldmap_elements(cache, &buf, &count);
    if (f.rc == NXT_OK && buf != nullptr)
    {
        f.placements.assign(buf, buf + count);
        nxt_free(buf);
    }
    return f;
}

bool isSameRecord(const nxt_wme_placement &a, const nxt_wme_placement &b)
{
    return a.area_id == b.area_id && a.element_id == b.element_id && a.world_x == b.world_x
           && a.world_y == b.world_y && a.plane == b.plane && a.flags == b.flags
           && a._pad == 0 && b._pad == 0;
}

void testArgGuardsWithCache(nxt_cache *cache)
{
    std::printf("Argument guards\n");
    nxt_wme_placement *buf = nullptr;
    size_t count = 0;
    check(nxt_get_worldmap_area_elements(cache, 28, nullptr, &count) == NXT_ERR_INVALID,
          "null out_placements -> NXT_ERR_INVALID");
    check(nxt_get_worldmap_area_elements(cache, 28, &buf, nullptr) == NXT_ERR_INVALID,
          "null out_count -> NXT_ERR_INVALID");
    const Fetched negative = fetchArea(cache, -1);
    check(negative.rc == NXT_ERR_INVALID && negative.isOutCleared,
          "negative area -> NXT_ERR_INVALID, outs cleared");
    const Fetched absent = fetchArea(cache, 1 << 20);
    check(absent.rc == NXT_ERR_NOT_FOUND && absent.isOutCleared,
          "absent area -> NXT_ERR_NOT_FOUND, outs cleared (rc=" + std::to_string(absent.rc) + ")");
}

// The full sweep must be exactly the per-area answers concatenated in ascending
// area order.
void testSweepMatchesAreas(nxt_cache *cache, const std::vector<nxt_wme_placement> &all)
{
    std::printf("Full sweep == per-area calls\n");
    int *ids = nullptr;
    size_t idCount = 0;
    if (nxt_list_archive_ids(cache, kWmeIndex, &ids, &idCount) != NXT_OK)
    {
        check(false, std::string("nxt_list_archive_ids(41): ") + nxt_last_error());
        return;
    }
    std::vector<int> areaIds(ids, ids + idCount);
    nxt_free(ids);
    std::sort(areaIds.begin(), areaIds.end());

    std::vector<nxt_wme_placement> joined;
    int areasWithElements = 0;
    for (int areaId : areaIds)
    {
        const Fetched f = fetchArea(cache, areaId);
        if (f.rc != NXT_OK)
        {
            check(false, "area " + std::to_string(areaId) + " rc=" + std::to_string(f.rc));
            return;
        }
        areasWithElements += f.placements.empty() ? 0 : 1;
        joined.insert(joined.end(), f.placements.begin(), f.placements.end());
    }
    bool isEqual = joined.size() == all.size();
    for (size_t i = 0; isEqual && i < all.size(); i++)
    {
        isEqual = isSameRecord(joined[i], all[i]);
    }
    check(isEqual, std::to_string(areaIds.size()) + " areas (" + std::to_string(areasWithElements)
                       + " with elements), " + std::to_string(joined.size()) + " records match");
}

// Resolves every distinct placed element id through nxt_get_worldmap_json.
std::map<int, WmeDef> resolveDefs(nxt_cache *cache, const std::vector<nxt_wme_placement> &all)
{
    std::printf("Every placed element id resolves to a WME definition\n");
    std::map<int, WmeDef> defs;
    size_t unresolvedPlacements = 0;
    for (const nxt_wme_placement &p : all)
    {
        if (defs.count(p.element_id) != 0)
        {
            continue;
        }
        char *json = nullptr;
        size_t len = 0;
        if (nxt_get_worldmap_json(cache, p.element_id, &json, &len) != NXT_OK)
        {
            defs[p.element_id] = {-2, ""};
            continue;
        }
        const nlohmann::json j = nlohmann::json::parse(std::string(json, len));
        nxt_free(json);
        // The decoder's "elementId" field is WME opcode 19, the category id.
        defs[p.element_id] = {j.value("elementId", -1), j.value("name", std::string())};
    }
    for (const nxt_wme_placement &p : all)
    {
        unresolvedPlacements += defs[p.element_id].category == -2 ? 1 : 0;
    }
    check(unresolvedPlacements == 0,
          std::to_string(all.size() - unresolvedPlacements) + "/" + std::to_string(all.size())
              + " placements resolve (" + std::to_string(defs.size()) + " distinct ids)");
    return defs;
}

struct Lodestone
{
    const char *town;
    int locId;      // the lodestone multiloc placed in the town
    int squareX;
    int squareY;
};

int chebyshev(int ax, int ay, int bx, int by)
{
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

void testLodestones(nxt_cache *cache, const std::vector<nxt_wme_placement> &all,
                    const std::map<int, WmeDef> &defs)
{
    std::printf("Lodestone placements vs the lodestone locs in index 5\n");
    const Lodestone lodestones[] = {
        {"Lumbridge", 69836, 50, 50},
        {"Varrock", 69840, 50, 52},
        {"Falador", 69835, 46, 53},
    };
    for (const Lodestone &l : lodestones)
    {
        nxt_loc_spawn *locs = nullptr;
        size_t locCount = 0;
        if (nxt_get_mapsquare_locs(cache, l.squareX, l.squareY, &locs, &locCount) != NXT_OK)
        {
            check(false, std::string(l.town) + ": map square not readable");
            continue;
        }
        std::vector<nxt_loc_spawn> square(locs, locs + locCount);
        nxt_free(locs);
        auto loc = std::find_if(square.begin(), square.end(),
                                [&](const nxt_loc_spawn &s) { return s.object_id == l.locId; });
        if (loc == square.end())
        {
            check(false, std::string(l.town) + ": lodestone loc " + std::to_string(l.locId)
                             + " not in its square");
            continue;
        }

        int best = 1 << 30;
        for (const nxt_wme_placement &p : all)
        {
            if (defs.at(p.element_id).category == kHomeTeleportCategory && p.plane == loc->plane)
            {
                best = std::min(best, chebyshev(p.world_x, p.world_y, loc->world_x, loc->world_y));
            }
        }
        check(best <= kLodestoneTolerance,
              std::string(l.town) + " lodestone loc at " + std::to_string(loc->world_x) + ","
                  + std::to_string(loc->world_y) + ": nearest HOME_TELEPORT_DESTINATION placement "
                  + std::to_string(best) + " tiles away");

        bool hasLabel = false;
        for (const nxt_wme_placement &p : all)
        {
            hasLabel = hasLabel
                       || (defs.at(p.element_id).name == l.town
                           && chebyshev(p.world_x, p.world_y, loc->world_x, loc->world_y)
                                  <= kLabelTolerance);
        }
        check(hasLabel, std::string(l.town) + " name label placed within "
                            + std::to_string(kLabelTolerance) + " tiles of its lodestone");
    }
}

}  // namespace

int main(int argc, char **argv)
{
    testDecoder();

    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(std::filesystem::path(path) / "js5-41.jcache"))
    {
        std::printf("SKIP: no js5-41.jcache in %s\n", path.c_str());
        return failures == 0 ? kExitSkip : 1;
    }
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (!cache)
    {
        std::printf("SKIP: cannot open %s: %s\n", path.c_str(), nxt_last_error());
        return failures == 0 ? kExitSkip : 1;
    }

    testArgGuardsWithCache(cache);
    const Fetched all = fetchAll(cache);
    check(all.rc == NXT_OK && !all.placements.empty(),
          "nxt_get_worldmap_elements -> " + std::to_string(all.placements.size()) + " placements");
    if (all.rc == NXT_OK && !all.placements.empty())
    {
        testSweepMatchesAreas(cache, all.placements);
        const std::map<int, WmeDef> defs = resolveDefs(cache, all.placements);
        testLodestones(cache, all.placements, defs);
    }
    nxt_cache_close(cache);

    std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
