// Tests for nxt_get_mapsquare_locs against a real cache. Drives the public C
// surface (the one Panama / GetProcAddress consumers bind) and cross-checks it
// against the C++ maps::buildMapSquareLocs it wraps, read through a second,
// independent RSCache so the two answers share no state.
//
//   nxtcache-mapsquare-locs-test [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP;
// ctest reports it as skipped via SKIP_RETURN_CODE, never as passed). The
// argument checks that need no cache run before the skip decision.

#include "c_api/nxtcache_c.h"
#include "core/RSCache.h"
#include "maps/MapSquare.h"
// RSCache.h only forward-declares these, but its inline constructor needs them
// complete to destroy its unique_ptr members.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kMapIndex = 5;
constexpr int kLumbridgeX = 50;
constexpr int kLumbridgeY = 50;

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

// One C-ABI call, its records copied out and the buffer freed.
struct LocsResult
{
    nxt_result rc{};
    bool isOutCleared{};   // *out_locs == NULL && *out_count == 0 after a failure
    std::vector<nxt_loc_spawn> locs;
};

LocsResult fetchLocs(nxt_cache *cache, int squareX, int squareY)
{
    // Poisoned so a failure path that forgets to clear them is caught.
    auto *locs = reinterpret_cast<nxt_loc_spawn *>(static_cast<uintptr_t>(0x1));
    size_t count = 12345;
    LocsResult result;
    result.rc = nxt_get_mapsquare_locs(cache, squareX, squareY, &locs, &count);
    result.isOutCleared = locs == nullptr && count == 0;
    if (result.rc == NXT_OK && locs != nullptr)
    {
        result.locs.assign(locs, locs + count);
        nxt_free(locs);
    }
    return result;
}

void testArgGuards()
{
    std::printf("Argument guards (no cache)\n");
    nxt_loc_spawn *locs = nullptr;
    size_t count = 0;
    check(nxt_get_mapsquare_locs(nullptr, kLumbridgeX, kLumbridgeY, &locs, &count) == NXT_ERR_INVALID,
          "null handle -> NXT_ERR_INVALID");
}

void testArgGuardsWithCache(nxt_cache *cache)
{
    std::printf("Argument guards\n");
    nxt_loc_spawn *locs = nullptr;
    size_t count = 0;
    check(nxt_get_mapsquare_locs(cache, kLumbridgeX, kLumbridgeY, nullptr, &count) == NXT_ERR_INVALID,
          "null out_locs -> NXT_ERR_INVALID");
    check(nxt_get_mapsquare_locs(cache, kLumbridgeX, kLumbridgeY, &locs, nullptr) == NXT_ERR_INVALID,
          "null out_count -> NXT_ERR_INVALID");

    const std::pair<int, int> outOfRange[] = {{-1, 50}, {128, 50}, {50, -1}, {50, 1024}};
    for (const auto &[x, y] : outOfRange)
    {
        const LocsResult r = fetchLocs(cache, x, y);
        check(r.rc == NXT_ERR_INVALID && r.isOutCleared,
              "square (" + std::to_string(x) + "," + std::to_string(y) + ") out of range -> "
                  "NXT_ERR_INVALID, outs cleared (rc=" + std::to_string(r.rc) + ")");
    }
}

// The C records must equal the C++ placements field for field, in order, and
// every record must sit inside the square it was asked for.
bool matchesCpp(const LocsResult &c, const std::vector<maps::LocSpawn> &cpp, int squareX,
                int squareY, std::string &outWhy)
{
    if (c.locs.size() != cpp.size())
    {
        outWhy = "count " + std::to_string(c.locs.size()) + " vs C++ " + std::to_string(cpp.size());
        return false;
    }
    for (size_t i = 0; i < cpp.size(); i++)
    {
        const nxt_loc_spawn &a = c.locs[i];
        const maps::LocSpawn &b = cpp[i];
        const bool isSame = a.object_id == b.objectId && a.world_x == b.worldX
                            && a.world_y == b.worldY && a.plane == b.plane
                            && a.shape == b.shape && a.rotation == b.rotation && a._pad == 0;
        const bool isInside = (a.world_x >> 6) == squareX && (a.world_y >> 6) == squareY
                              && a.plane < 4 && a.rotation < 4;
        if (!isSame || !isInside)
        {
            outWhy = "record " + std::to_string(i) + " (loc " + std::to_string(a.object_id) + " at "
                     + std::to_string(a.world_x) + "," + std::to_string(a.world_y) + ") "
                     + (isSame ? "outside the square" : "differs from C++");
            return false;
        }
    }
    return true;
}

void testKnownSquare(nxt_cache *cache, RSCache &cpp)
{
    std::printf("Known square (Lumbridge %d,%d)\n", kLumbridgeX, kLumbridgeY);
    const LocsResult r = fetchLocs(cache, kLumbridgeX, kLumbridgeY);
    check(r.rc == NXT_OK, "rc == NXT_OK (rc=" + std::to_string(r.rc) + ")");
    check(!r.locs.empty(), std::to_string(r.locs.size()) + " placements (expected > 0)");
    std::string why;
    const bool isMatch = matchesCpp(r, maps::buildMapSquareLocs(cpp, kLumbridgeX, kLumbridgeY),
                                    kLumbridgeX, kLumbridgeY, why);
    check(isMatch, "records inside the square and equal to maps::buildMapSquareLocs" +
                       (isMatch ? std::string() : ": " + why));
}

// Every square the index lists: the C entry reports NXT_OK exactly when the
// C++ decode finds the square, and then publishes the same records. Returns
// the listed squares as archive ids.
std::set<int> testSweep(nxt_cache *cache, RSCache &cpp)
{
    std::printf("Sweep of every index-5 archive\n");
    std::set<int> listed;
    for (int archiveId : cpp.archiveIds(kMapIndex))
    {
        listed.insert(archiveId);
    }
    size_t squares = 0;
    size_t records = 0;
    size_t emptySquares = 0;
    size_t unreadable = 0;
    int mismatches = 0;
    for (int archiveId : listed)
    {
        const int x = archiveId & 0x7F;
        const int y = archiveId >> 7;
        const LocsResult r = fetchLocs(cache, x, y);
        std::vector<maps::LocSpawn> expected;
        const bool isPresent = maps::buildMapSquareLocs(cpp, x, y, expected);
        std::string why = "rc=" + std::to_string(r.rc) + " but C++ present=" + std::to_string(isPresent);
        const bool isOk = isPresent ? r.rc == NXT_OK && matchesCpp(r, expected, x, y, why)
                                    : r.rc == NXT_ERR_NOT_FOUND && r.isOutCleared;
        cpp.evictArchive(kMapIndex, archiveId);
        squares++;
        records += r.locs.size();
        emptySquares += isPresent && r.locs.empty() ? 1 : 0;
        unreadable += isPresent ? 0 : 1;
        if (!isOk && mismatches++ < 5)
        {
            std::printf("    square (%d,%d): rc=%d %s\n", x, y, r.rc, why.c_str());
        }
    }
    check(squares > unreadable, std::to_string(squares) + " listed squares swept, "
                                    + std::to_string(unreadable) + " listed but not readable");
    check(mismatches == 0, std::to_string(records) + " records, " + std::to_string(emptySquares)
                               + " empty squares, " + std::to_string(mismatches)
                               + " squares disagreeing with C++ or outside their square");
    return listed;
}

void testAbsent(nxt_cache *cache, RSCache &cpp, const std::set<int> &listed)
{
    std::printf("Absent square\n");
    // The first in-range square the index does not list, found rather than
    // hard-coded so the check survives cache updates.
    int absentX = -1;
    int absentY = -1;
    for (int archiveId = 0; archiveId < 128 * 1024 && absentX < 0; archiveId++)
    {
        if (listed.count(archiveId) == 0)
        {
            absentX = archiveId & 0x7F;
            absentY = archiveId >> 7;
        }
    }
    const LocsResult r = fetchLocs(cache, absentX, absentY);
    check(r.rc == NXT_ERR_NOT_FOUND && r.isOutCleared,
          "square (" + std::to_string(absentX) + "," + std::to_string(absentY)
              + ") -> NXT_ERR_NOT_FOUND, outs cleared (rc=" + std::to_string(r.rc) + ")");
    check(maps::buildMapSquareLocs(cpp, absentX, absentY).empty(),
          "C++ vector overload still returns empty for the absent square");
}

}  // namespace

int main(int argc, char **argv)
{
    testArgGuards();

    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(std::filesystem::path(path) / "js5-5.jcache"))
    {
        std::printf("SKIP: no cache at %s (js5-5.jcache absent). Pass a cache dir or set "
                    "NXTCACHE_TEST_CACHE. Only the no-cache guards ran (%d failure(s)).\n",
                    path.c_str(), failures);
        return failures == 0 ? kExitSkip : 1;
    }
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (cache == nullptr)
    {
        std::printf("FAIL: nxt_cache_open_local(%s): %s\n", path.c_str(), nxt_last_error());
        return 1;
    }
    std::printf("cache: %s\n", path.c_str());
    RSCache cpp(path);
    testArgGuardsWithCache(cache);
    testKnownSquare(cache, cpp);
    const std::set<int> listed = testSweep(cache, cpp);
    testAbsent(cache, cpp, listed);
    nxt_cache_close(cache);
    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
