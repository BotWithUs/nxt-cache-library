// Ground-truth tests for the varp (VarPlayerType) C ABI, run against a real
// cache. Exercises only the public C surface, the same one Panama and
// GetProcAddress consumers bind.
//
//   nxtcache-varp-test [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP;
// ctest reports it as skipped via SKIP_RETURN_CODE, never as passed).
//
// The gameval cross-check needs the beta JS5 endpoint (the live cache carries
// no index 67). It runs only with NXTCACHE_TEST_BETA=1 and prints a SKIPPED
// line otherwise, or when the beta is unreachable.

#include "c_api/nxtcache_c.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kTypeInt = 0;
constexpr int kTypeBoolean = 1;
constexpr int kTypeString = 36;
constexpr int kTypeStruct = 73;
constexpr int kTypeLong = 110;

int failures = 0;
int skippedChecks = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

void skipCheck(const std::string &what)
{
    std::printf("  SKIPPED CHECK  %s\n", what.c_str());
    skippedChecks++;
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

nxt_result info(nxt_cache *cache, int id, nxt_varp_info &outInfo)
{
    outInfo = nxt_varp_info{};
    outInfo.struct_size = sizeof(nxt_varp_info);
    return nxt_get_varp_info(cache, id, &outInfo);
}

// A varp that must exist with an exact type, base type, rule and default.
void expectVarp(nxt_cache *cache, int id, int typeId, int baseType, int rule, int64_t def)
{
    nxt_varp_info v{};
    const nxt_result rc = info(cache, id, v);
    const std::string tag = "varp " + std::to_string(id);
    // nxt_last_error() is only meaningful after a failure; on OK it is stale.
    check(rc == NXT_OK, tag + " info rc=" + std::to_string(rc) +
                            (rc == NXT_OK ? std::string() : std::string(" ") + nxt_last_error()));
    if (rc != NXT_OK)
    {
        return;
    }
    check(v.struct_size == sizeof(nxt_varp_info) && v.version == NXT_VARP_INFO_VERSION,
          tag + " struct_size/version stamped");
    check(v.id == id, tag + " id echoed");
    check(v.type_id == typeId, tag + " type_id=" + std::to_string(v.type_id) +
                                   " expected " + std::to_string(typeId));
    check(v.base_type == baseType, tag + " base_type=" + std::to_string(v.base_type));
    check(v.default_rule == rule, tag + " default_rule=" + std::to_string(v.default_rule));
    check(v.default_value == def, tag + " default=" + std::to_string(v.default_value) +
                                      " expected " + std::to_string(def));
    int32_t exists = -1;
    check(nxt_varp_exists(cache, id, &exists) == NXT_OK && exists == 1, tag + " exists == 1");
}

void expectAbsent(nxt_cache *cache, int id)
{
    nxt_varp_info v{};
    const std::string tag = "varp " + std::to_string(id);
    const nxt_result rc = info(cache, id, v);
    check(rc == NXT_ERR_NOT_FOUND, tag + " info -> NXT_ERR_NOT_FOUND (rc=" + std::to_string(rc) + ")");
    int32_t exists = -1;
    const nxt_result erc = nxt_varp_exists(cache, id, &exists);
    check(erc == NXT_OK && exists == 0, tag + " exists -> NXT_OK, 0");
}

void testAbiGuards(nxt_cache *cache)
{
    std::printf("ABI guards\n");
    nxt_varp_info v{};
    v.struct_size = sizeof(nxt_varp_info) - 1;
    v.type_id = 12345;
    check(nxt_get_varp_info(cache, 2492, &v) == NXT_ERR_INVALID, "short struct_size -> NXT_ERR_INVALID");
    check(v.type_id == 12345, "struct untouched on error");
    check(nxt_get_varp_info(nullptr, 2492, &v) == NXT_ERR_INVALID, "null handle -> NXT_ERR_INVALID");
    check(nxt_get_varp_info(cache, 2492, nullptr) == NXT_ERR_INVALID, "null out -> NXT_ERR_INVALID");
    check(nxt_varp_exists(cache, 2492, nullptr) == NXT_ERR_INVALID, "exists null out -> NXT_ERR_INVALID");
}

void testKnownVarps(nxt_cache *cache)
{
    std::printf("Known varps\n");
    // COOKQUEST: absent from the live client until the quest starts, then 1.
    expectVarp(cache, 2492, kTypeInt, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, 0);
    // OPTION_RUN, and the RE's live samples (INT 3/10/14/28, STRUCT 11967).
    expectVarp(cache, 463, kTypeInt, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, 0);
    expectVarp(cache, 3, kTypeInt, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, 0);
    expectVarp(cache, 10, kTypeInt, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, 0);
    expectVarp(cache, 14, kTypeInt, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, 0);
    expectVarp(cache, 28, kTypeInt, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, 0);
    expectVarp(cache, 11967, kTypeStruct, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_TYPE, -1);

    char *json = nullptr;
    size_t len = 0;
    const nxt_result rc = nxt_get_json(cache, "varp", 2492, &json, &len);
    check(rc == NXT_OK && json != nullptr && std::string(json).find("\"typeId\":0") != std::string::npos,
          "nxt_get_json(\"varp\", 2492) carries typeId 0");
    nxt_free(json);
}

struct Sweep
{
    std::vector<int> ids;
    std::map<int, int> typeCounts;
    int booleanWithoutOp7 = 0;
    int unknownBase = 0;
    int decodeFailures = 0;
    int ruleMismatches = 0;
    int defaultsOutsideInt32 = 0;
    int longBase = 0;
    int firstBoolean = -1;
    int firstString = -1;
    int firstLong = -1;
};

// ScriptVarType defaults, restated here independently of the library table:
// INT, BOOLEAN, STRING (value field 0, text ""), LONG, TELEMETRY_INTERVAL and
// WORLD_AREA default to 0; COORDFINE has none; every other type defaults to -1.
constexpr int kTypeCoordFine = 50;

int64_t expectedTypeDefault(int typeId)
{
    switch (typeId)
    {
        case kTypeInt:
        case kTypeBoolean:
        case kTypeString:
        case kTypeLong:
        case 126:
        case 127:
            return 0;
        default:
            return -1;
    }
}

// Re-derive the client rule from the raw fields and compare it with the
// library's answer, independently of how the library computes it.
bool ruleHolds(const nxt_varp_info &v)
{
    if (v.op7_absent != 0 && v.type_id == kTypeBoolean)
    {
        return v.default_rule == NXT_VARP_DEFAULT_DOMAIN && v.default_value == -1;
    }
    if (v.type_id == kTypeCoordFine)
    {
        return v.default_rule == NXT_VARP_DEFAULT_NONE && v.default_value == 0;
    }
    return v.default_rule == NXT_VARP_DEFAULT_TYPE && v.default_value == expectedTypeDefault(v.type_id);
}

Sweep sweepAll(nxt_cache *cache)
{
    Sweep s;
    int *ids = nullptr;
    size_t count = 0;
    if (nxt_list_type_ids(cache, "varp", &ids, &count) != NXT_OK)
    {
        return s;
    }
    s.ids.assign(ids, ids + count);
    nxt_free(ids);
    for (int id : s.ids)
    {
        nxt_varp_info v{};
        if (info(cache, id, v) != NXT_OK)
        {
            s.decodeFailures++;
            continue;
        }
        s.typeCounts[v.type_id]++;
        s.unknownBase += v.base_type == NXT_VAR_BASE_UNKNOWN ? 1 : 0;
        s.ruleMismatches += ruleHolds(v) ? 0 : 1;
        s.longBase += v.base_type == NXT_VAR_BASE_LONG ? 1 : 0;
        s.defaultsOutsideInt32 += (v.default_value < INT32_MIN || v.default_value > INT32_MAX) ? 1 : 0;
        if (v.type_id == kTypeBoolean && v.op7_absent != 0)
        {
            s.booleanWithoutOp7++;
        }
        if (v.type_id == kTypeBoolean && s.firstBoolean < 0)
        {
            s.firstBoolean = id;
        }
        if (v.type_id == kTypeString && s.firstString < 0)
        {
            s.firstString = id;
        }
        if (v.type_id == kTypeLong && s.firstLong < 0)
        {
            s.firstLong = id;
        }
    }
    return s;
}

Sweep testSweep(nxt_cache *cache)
{
    std::printf("Full sweep\n");
    Sweep s = sweepAll(cache);
    std::printf("  defined varps: %zu (max id %d)\n", s.ids.size(), s.ids.empty() ? -1 : s.ids.back());
    std::printf("  BOOLEAN varps without opcode 7 (default -1): %d\n", s.booleanWithoutOp7);
    check(s.ids.size() > 10000, "more than 10000 varps defined");
    check(s.decodeFailures == 0, "every listed varp decodes (failures=" + std::to_string(s.decodeFailures) + ")");
    check(s.unknownBase == 0, "every type id is in the ScriptVarType table (unknown=" +
                                  std::to_string(s.unknownBase) + ")");
    std::printf("  LONG-based varps: %d\n", s.longBase);
    check(s.defaultsOutsideInt32 == 0, "every default_value fits int32, LONG types included (outside=" +
                                           std::to_string(s.defaultsOutsideInt32) + ")");
    check(s.ruleMismatches == 0, "client default rule holds for every varp (mismatches=" +
                                     std::to_string(s.ruleMismatches) + ")");
    check(s.typeCounts[kTypeInt] > s.typeCounts[kTypeBoolean], "INT is the dominant type");
    if (s.firstBoolean >= 0)
    {
        expectVarp(cache, s.firstBoolean, kTypeBoolean, NXT_VAR_BASE_INTEGER, NXT_VARP_DEFAULT_DOMAIN, -1);
    }
    if (s.firstString >= 0)
    {
        expectVarp(cache, s.firstString, kTypeString, NXT_VAR_BASE_STRING, NXT_VARP_DEFAULT_TYPE, 0);
    }
    if (s.firstLong >= 0)
    {
        expectVarp(cache, s.firstLong, kTypeLong, NXT_VAR_BASE_LONG, NXT_VARP_DEFAULT_TYPE, 0);
    }
    return s;
}

void testAbsent(nxt_cache *cache, const Sweep &s)
{
    std::printf("Absent ids\n");
    if (s.ids.empty())
    {
        check(false, "no ids to derive absent ids from");
        return;
    }
    expectAbsent(cache, s.ids.back() + 1);
    expectAbsent(cache, 65536);
    expectAbsent(cache, 0x3FFFFFFF);
    expectAbsent(cache, -1);
    const std::set<int> present(s.ids.begin(), s.ids.end());
    for (int id = 0; id < s.ids.back(); id++)
    {
        if (!present.contains(id))
        {
            expectAbsent(cache, id);  // first gap inside the defined range
            break;
        }
    }
}

// An unreadable cache must answer NXT_ERR_IO, never "no such varp".
void testIoIsNotNotFound()
{
    std::printf("I/O error is distinct from not-found\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "nxtcache-varp-test-empty";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    nxt_cache *empty = nxt_cache_open_local(dir.string().c_str());
    if (empty == nullptr)
    {
        skipCheck("open_local on an empty dir returned NULL; I/O path not exercised");
        return;
    }
    int32_t exists = -1;
    const nxt_result erc = nxt_varp_exists(empty, 2492, &exists);
    check(erc == NXT_ERR_IO && exists == -1, "empty cache: exists -> NXT_ERR_IO, out untouched (rc=" +
                                                 std::to_string(erc) + ")");
    nxt_varp_info v{};
    const nxt_result irc = info(empty, 2492, v);
    check(irc == NXT_ERR_IO, "empty cache: info -> NXT_ERR_IO (rc=" + std::to_string(irc) + ")");
    nxt_cache_close(empty);
    fs::remove_all(dir, ec);
}

std::set<int> listVarps(nxt_cache *cache)
{
    int *ids = nullptr;
    size_t count = 0;
    std::set<int> out;
    if (nxt_list_type_ids(cache, "varp", &ids, &count) == NXT_OK)
    {
        out.insert(ids, ids + count);
        nxt_free(ids);
    }
    return out;
}

// Names and varps are compared within ONE build (the beta), because the live
// cache has no index 67 and live and beta builds drift. Measured on build 947:
// the var_player group also names ~50k '_'-prefixed ids ABOVE the last varp id,
// which are not varps. So the predicate is "every name at or below the last
// varp id is a defined varp", not "every name is a varp".
void compareGamevals(const nlohmann::json &entries, const std::set<int> &betaVarps,
                     const std::set<int> &liveVarps)
{
    const int maxVarp = *betaVarps.rbegin();
    std::vector<int> missing;
    size_t inRange = 0;
    size_t beyond = 0;
    size_t liveDrift = 0;
    for (const auto &[key, name] : entries.items())
    {
        const int id = std::stoi(key);
        if (id > maxVarp)
        {
            beyond++;
            continue;
        }
        inRange++;
        missing.push_back(betaVarps.contains(id) ? -1 : id);
        liveDrift += liveVarps.contains(id) ? 0 : 1;
    }
    std::erase(missing, -1);
    std::printf("  beta: %zu varps (max %d); %zu names at or below it, %zu names above it (not varps)\n",
                betaVarps.size(), maxVarp, inRange, beyond);
    std::printf("  informational: %zu in-range beta names are not varps in the live cache (build drift)\n",
                liveDrift);
    check(missing.empty(), "every beta var_player name <= last varp id is a beta varp (missing=" +
                               std::to_string(missing.size()) + ")");
    check(entries.value("2492", "") == "COOKQUEST", "gameval 2492 == COOKQUEST (id alignment)");
    check(entries.value("463", "") == "OPTION_RUN", "gameval 463 == OPTION_RUN (id alignment)");
}

void testGamevals(const Sweep &s)
{
    std::printf("Gameval cross-check (beta var_player names vs beta varps)\n");
    const char *flag = std::getenv("NXTCACHE_TEST_BETA");
    if (flag == nullptr || std::string(flag) != "1")
    {
        skipCheck("gameval cross-check: set NXTCACHE_TEST_BETA=1 to fetch beta gamevals over the network");
        return;
    }
    nxt_cache *beta = nxt_cache_open_live_beta();
    if (beta == nullptr)
    {
        skipCheck(std::string("gameval cross-check: beta unreachable: ") + nxt_last_error());
        return;
    }
    char *json = nullptr;
    size_t len = 0;
    const nxt_result rc = nxt_get_gameval_group_json(beta, "var_player", &json, &len);
    const std::set<int> betaVarps = rc == NXT_OK ? listVarps(beta) : std::set<int>{};
    if (rc != NXT_OK || betaVarps.empty())
    {
        skipCheck(std::string("gameval cross-check: beta var_player names or varps unavailable: ") +
                  nxt_last_error());
        nxt_free(json);
        nxt_cache_close(beta);
        return;
    }
    const auto doc = nlohmann::json::parse(json);
    nxt_free(json);
    compareGamevals(doc["entries"], betaVarps, std::set<int>(s.ids.begin(), s.ids.end()));
    nxt_cache_close(beta);
}

}  // namespace

int main(int argc, char **argv)
{
    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(std::filesystem::path(path) / "js5-2.jcache"))
    {
        std::printf("SKIP: no cache at %s (js5-2.jcache absent). Pass a cache dir or set "
                    "NXTCACHE_TEST_CACHE. Nothing was tested.\n", path.c_str());
        return kExitSkip;
    }
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (cache == nullptr)
    {
        std::printf("FAIL: nxt_cache_open_local(%s): %s\n", path.c_str(), nxt_last_error());
        return 1;
    }
    std::printf("cache: %s\n", path.c_str());
    testAbiGuards(cache);
    testKnownVarps(cache);
    const Sweep sweep = testSweep(cache);
    testAbsent(cache, sweep);
    testIoIsNotNotFound();
    testGamevals(sweep);
    nxt_cache_close(cache);
    std::printf("\n%s: %d failure(s), %d skipped check(s)\n",
                failures == 0 ? "PASS" : "FAIL", failures, skippedChecks);
    return failures == 0 ? 0 : 1;
}
