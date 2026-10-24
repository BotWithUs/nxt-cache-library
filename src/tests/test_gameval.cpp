// Gameval tests: the index-67 decoder, the C ABI group getter and the
// gameval.sqlite builder, run against a real local cache.
//
//   nxtcache-gameval-test [cache-dir]
//   nxtcache-gameval-test --offline
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// The cache is only read; the database is baked into the temp directory and
// deleted afterwards. Exit codes: 0 all checks passed, 1 a check failed,
// 77 js5-67.jcache absent (SKIP; ctest reports it as skipped, never as passed).
// A js5-67.jcache that is present but unreadable, truncated or wrong is a FAIL.
//
// --offline runs the bit-table split on synthetic tables (no cache needed).

#include "c_api/nxtcache_c.h"
#include "config_types/GameVal.h"
#include "config_types/GameValDb.h"
#include "config_types/Types.h"
#include "core/RSCache.h"
#include "network/Js5Cache.h"  // completes RSCache's Js5Socket member

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;

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

int finish()
{
    std::printf("\n%s: %d failure(s), %d skipped check(s)\n",
                failures == 0 ? "PASS" : "FAIL", failures, skippedChecks);
    return failures == 0 ? 0 : 1;
}

// ---- offline: the bit-table split ------------------------------------------

std::map<int, std::string> asMap(const std::vector<nxt::GameValEntry> &entries)
{
    std::map<int, std::string> out;
    for (const auto &e : entries)
    {
        out[e.id] = e.name;
    }
    return out;
}

void testSplitOffline()
{
    std::printf("Bit-table split (synthetic)\n");
    // Vars 0..2 (id 1 unnamed), so N = 3. Raw 5 and 7 are varbits 2 and 4:
    // varbits 0, 1 and 3 belong to other domains and are unnamed holes. The
    // first '_' id (5) is NOT the base.
    const nxt::GameValSplit s = nxt::splitGameValBitTable(
        {{0, "VAR_A"}, {2, "VAR_C"}, {5, "_BIT_TWO"}, {7, "_BIT_FOUR"}});
    check(s.isSplit, "a table with every '_' id above every var id splits");
    check(s.bitBase == 3, "bit base is last var id + 1 (3), not the first '_' id (got " +
                              std::to_string(s.bitBase) + ")");
    check(asMap(s.vars) == std::map<int, std::string>{{0, "VAR_A"}, {2, "VAR_C"}},
          "vars keep their ids");
    check(asMap(s.bits) == std::map<int, std::string>{{2, "BIT_TWO"}, {4, "BIT_FOUR"}},
          "bits are re-based to varbit ids with the '_' stripped");

    // Several plain groups (category, graphic, material, model, sound) carry
    // '_' dev names among ordinary ones: those must stay whole.
    const nxt::GameValSplit mixed =
        nxt::splitGameValBitTable({{0, "A"}, {1, "_TEST_DEV"}, {2, "B"}});
    check(!mixed.isSplit && mixed.vars.size() == 3 && mixed.bits.empty(),
          "a table with '_' names interleaved among plain ones is not split");

    check(nxt::gameValEtype("obj") == "item" && nxt::gameValEtype("var_player") == "varp" &&
              nxt::gameValEtype("loc") == "loc",
          "etype renames: obj -> item, var_player -> varp, others identity");
    check(nxt::gameValGroupName(29) == "mapelement", "archive 29 is mapelement");
}

// ---- C ABI on the local cache ----------------------------------------------

void testCApi(const std::string &path)
{
    std::printf("C ABI on the local cache\n");
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (cache == nullptr)
    {
        check(false, std::string("nxt_cache_open_local: ") + nxt_last_error());
        return;
    }
    char *json = nullptr;
    size_t len = 0;
    nxt_result rc = nxt_get_gameval_group_json(cache, "interface", &json, &len);
    check(rc == NXT_OK, "nxt_get_gameval_group_json(interface) on a local cache (no beta)");
    if (rc == NXT_OK)
    {
        const auto doc = nlohmann::json::parse(json, nullptr, false);
        const bool isBank = !doc.is_discarded() && doc.contains("entries") &&
                            doc["entries"].value("517", "") == "BANK";
        check(isBank, "C ABI interface 517 == BANK");
    }
    nxt_free(json);
    json = nullptr;
    rc = nxt_get_gameval_group_json(cache, "mapelement", &json, &len);
    check(rc == NXT_OK, "nxt_get_gameval_group_json(mapelement) resolves (archive 29)");
    nxt_free(json);
    nxt_cache_close(cache);
}

// ---- the baked database ----------------------------------------------------

struct Db
{
    sqlite3 *handle = nullptr;
    ~Db() { sqlite3_close(handle); }
};

// First column of the first row as text, or nullopt for no row / an error.
std::optional<std::string> scalar(sqlite3 *db, const std::string &sql,
                                  const std::vector<std::string> &args = {})
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::printf("    sqlite: %s\n", sqlite3_errmsg(db));
        return std::nullopt;
    }
    for (size_t i = 0; i < args.size(); i++)
    {
        sqlite3_bind_text(stmt, static_cast<int>(i) + 1, args[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    std::optional<std::string> out;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0) != nullptr)
    {
        out = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<std::string> column(sqlite3 *db, const std::string &sql, int col)
{
    std::vector<std::string> out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char *t = sqlite3_column_text(stmt, col);
        out.emplace_back(t != nullptr ? reinterpret_cast<const char *>(t) : "");
    }
    sqlite3_finalize(stmt);
    return out;
}

// The host's own name -> id statement (SqliteGamevalIndex.SQL_ID).
std::optional<long long> idOf(sqlite3 *db, const std::string &etype, const std::string &name)
{
    auto v = scalar(db, "SELECT eid FROM gameval WHERE etype = ? AND name = ? LIMIT 1",
                    {etype, name});
    return v ? std::optional<long long>(std::stoll(*v)) : std::nullopt;
}

void expectId(sqlite3 *db, const std::string &etype, const std::string &name, long long want)
{
    const auto got = idOf(db, etype, name);
    check(got && *got == want, etype + " " + name + " == " + std::to_string(want) + " (got " +
                                   (got ? std::to_string(*got) : std::string("none")) + ")");
}

void testSchema(sqlite3 *db)
{
    std::printf("Schema v1 (SqliteGamevalIndex contract)\n");
    check(scalar(db, "SELECT value FROM meta WHERE key = 'schema_version'") == "1",
          "meta.schema_version == 1");
    check(column(db, "PRAGMA table_info(gameval)", 1) ==
              std::vector<std::string>{"etype", "eid", "name"},
          "gameval(etype, eid, name)");
    check(column(db, "PRAGMA index_info(idx_gameval_lookup)", 2) ==
              std::vector<std::string>{"etype", "name"},
          "idx_gameval_lookup on (etype, name)");
    check(scalar(db, "SELECT value FROM meta WHERE key = 'rows'") ==
              scalar(db, "SELECT count(*) FROM gameval"),
          "meta.rows == count(*)");
    check(scalar(db, "SELECT value FROM meta WHERE key = 'groups'") ==
              scalar(db, "SELECT count(DISTINCT etype) FROM gameval"),
          "meta.groups == distinct etypes");
    check(scalar(db, "SELECT count(*) FROM gameval WHERE name <> upper(name)") == "0",
          "every name is UPPERCASE");
}

void testEtypes(sqlite3 *db)
{
    std::printf("Etypes\n");
    const std::vector<std::string> got =
        column(db, "SELECT DISTINCT etype FROM gameval ORDER BY etype", 0);
    const std::set<std::string> have(got.begin(), got.end());
    std::set<std::string> want;
    for (const auto &[aid, group] : nxt::gameValGroups())
    {
        want.insert(nxt::gameValEtype(group));
    }
    for (const auto &t : nxt::gameValBitTables())
    {
        want.insert(t.bitEtype);
    }
    check(have == want, "all " + std::to_string(want.size()) + " etypes present, no others (got " +
                            std::to_string(have.size()) + ")");
    check(!have.contains("obj") && !have.contains("var_player"),
          "no 'obj' / 'var_player' etype (renamed item / varp)");
}

void testKnownIds(sqlite3 *db)
{
    std::printf("Known ids (live 951-1; also in the beta-built DB)\n");
    expectId(db, "interface", "BANK", 517);
    const auto bankComps = column(db, "SELECT eid FROM gameval WHERE etype = 'component' AND "
                                      "name >= 'BANK__' AND name < 'BANK_`'", 0);
    bool isAllBank = !bankComps.empty();
    for (const auto &eid : bankComps)
    {
        isAllBank = isAllBank && (std::stoll(eid) >> 16) == 517;
    }
    check(isAllBank, std::to_string(bankComps.size()) + " BANK__ components, every eid >> 16 == 517");
    expectId(db, "component", "BANK__ALL", 517LL << 16);

    expectId(db, "interface", "ESCAPE_MENU", 1433);
    // Interface-relative component 69 of whatever ESCAPE_MENU resolves to (1433
    // when it does not, so the check still runs and fails on its own).
    const long long esc = idOf(db, "interface", "ESCAPE_MENU").value_or(1433);
    expectId(db, "component", "ESCAPE_MENU__LOGOUT_LOBBY", (esc << 16) | 69);
    expectId(db, "item", "COINS", 995);
    expectId(db, "item", "ABYSSAL_WHIP", 4151);
    expectId(db, "varp", "COOKQUEST", 2492);
    expectId(db, "varp", "OPTION_RUN", 463);
    expectId(db, "mapelement", "DRAYNORMANOR", 100);
    check(scalar(db, "SELECT name FROM gameval WHERE etype = 'loc' AND eid = 0").has_value(),
          "id -> name lookup works (loc 0 is named)");
}

// Ground truth for the bit-table base: every row of a bit etype must be a
// varbit config (index 2 archive 69) of that table's domain.
void testBitTables(sqlite3 *db, RSCache &cache)
{
    std::printf("Bit tables vs varbit configs\n");
    Archive &archive = cache.archive(2, 69);
    if (archive.id == -1)
    {
        skipCheck("bit tables: no varbit configs (js5-2.jcache) beside js5-67.jcache");
        return;
    }
    std::map<int, int> domains;
    for (int fid : archive.fileIds)
    {
        RSBuffer buffer = archive.readFile(fid);
        if (buffer.buffer != nullptr && buffer.remaining() != 0)
        {
            VarbitType v;
            v.decode(buffer);
            domains[fid] = v.domainType;
        }
    }
    for (const auto &t : nxt::gameValBitTables())
    {
        const auto eids =
            column(db, std::string("SELECT eid FROM gameval WHERE etype = '") + t.bitEtype + "'", 0);
        size_t matches = 0;
        for (const auto &eid : eids)
        {
            auto it = domains.find(std::stoi(eid));
            matches += (it != domains.end() && it->second == t.varbitDomain) ? 1 : 0;
        }
        check(!eids.empty() && matches == eids.size(),
              std::string(t.bitEtype) + ": " + std::to_string(matches) + "/" +
                  std::to_string(eids.size()) + " are varbits of domain " +
                  std::to_string(t.varbitDomain));
    }
}

void testDatabase(const std::string &path)
{
    std::printf("gameval.sqlite builder\n");
    const std::string unique = std::to_string(std::random_device{}());
    const std::filesystem::path out =
        std::filesystem::temp_directory_path() / ("nxtcache-gameval-test-" + unique + ".sqlite");
    RSCache cache(path);
    try
    {
        const nxt::GameValDbResult r = nxt::buildGameValDb(cache, out, {{"source", "test"}});
        check(true, "buildGameValDb: " + std::to_string(r.rows) + " rows, bit tables " +
                        r.bitTableCheck);
    }
    catch (const std::exception &e)
    {
        check(false, std::string("buildGameValDb threw: ") + e.what());
        return;
    }
    {
        Db db;
        const std::u8string u8 = out.u8string();
        if (sqlite3_open_v2(reinterpret_cast<const char *>(u8.c_str()), &db.handle,
                            SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        {
            check(false, "open the baked database read-only");
        }
        else
        {
            testSchema(db.handle);
            testEtypes(db.handle);
            testKnownIds(db.handle);
            testBitTables(db.handle, cache);
        }
    }
    std::error_code ec;
    std::filesystem::remove(out, ec);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--offline")
    {
        testSplitOffline();
        return finish();
    }
    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(std::filesystem::path(path) / "js5-67.jcache"))
    {
        std::printf("SKIP: no js5-67.jcache in %s. Pass a cache dir or set NXTCACHE_TEST_CACHE. "
                    "Nothing was tested.\n", path.c_str());
        return kExitSkip;
    }
    std::printf("cache: %s\n", path.c_str());
    testCApi(path);
    testDatabase(path);
    return finish();
}
