// nxt_build_gameval_db + nxt_gameval_index_version, through the C ABI only.
//
//   nxtcache-gameval-c-api-test <nxtcache-dumper> [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// The cache is only read; every database goes to a fresh temp directory that is
// deleted afterwards.
//
// Checks that need no cache run first and always: argument validation and the
// "index 67 absent" result, against an empty directory. Then, when the cache has
// js5-67.jcache:
//   - the export's database is row-identical to `nxtcache-dumper --type gameval
//     --sqlite` from the same cache (same schema, rows, and meta but for
//     generator/built),
//   - the probe equals meta.index_version,
//   - an existing file is replaced; a held-open one (Windows) and an unwritable
//     path fail with NXT_ERR_IO and leave nothing behind.
// Exit codes: 0 all passed, 1 a check failed, 77 js5-67.jcache absent (SKIP).

#include "c_api/nxtcache_c.h"

#include <sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace
{

constexpr int kExitSkip = 77;

int failures = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

std::string utf8(const fs::path &p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path partialOf(const fs::path &p)
{
    fs::path out = p;
    out += ".partial";
    return out;
}

bool isPresent(const fs::path &p)
{
    std::error_code ec;
    return fs::exists(p, ec);
}

std::vector<char> fileBytes(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Runs exe with args (no shell); returns the exit code, or -1 if it did not run.
int run(const fs::path &exe, const std::vector<std::string> &args)
{
#ifdef _WIN32
    // _spawnv joins argv with spaces, so quote each one (none of ours holds a quote).
    std::vector<std::string> quoted;
    quoted.push_back("\"" + exe.string() + "\"");
    for (const std::string &a : args)
    {
        quoted.push_back("\"" + a + "\"");
    }
    std::vector<const char *> argv;
    for (const std::string &q : quoted)
    {
        argv.push_back(q.c_str());
    }
    argv.push_back(nullptr);
    std::fflush(stdout);
    return static_cast<int>(_spawnv(_P_WAIT, exe.string().c_str(), argv.data()));
#else
    std::vector<std::string> all{exe.string()};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (std::string &a : all)
    {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    std::fflush(stdout);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
    {
        return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// ---- reading a baked database back ------------------------------------------

using Row = std::tuple<std::string, long long, std::string>;

struct Db
{
    bool isOpen = false;
    std::vector<Row> rows;                     // gameval, ordered by (etype, eid)
    std::map<std::string, std::string> meta;
    std::vector<std::string> schema;           // sqlite_master type|name|sql, by name
};

std::string column(sqlite3_stmt *stmt, int i)
{
    const unsigned char *t = sqlite3_column_text(stmt, i);
    return t != nullptr ? reinterpret_cast<const char *>(t) : "<null>";
}

Db readDb(const fs::path &file)
{
    Db db;
    sqlite3 *con = nullptr;
    if (sqlite3_open_v2(utf8(file).c_str(), &con, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        sqlite3_close(con);
        return db;
    }
    db.isOpen = true;
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(con, "SELECT etype, eid, name FROM gameval ORDER BY etype, eid", -1, &stmt, nullptr);
    while (stmt != nullptr && sqlite3_step(stmt) == SQLITE_ROW)
    {
        db.rows.emplace_back(column(stmt, 0), sqlite3_column_int64(stmt, 1), column(stmt, 2));
    }
    sqlite3_finalize(stmt);
    sqlite3_prepare_v2(con, "SELECT key, value FROM meta", -1, &stmt, nullptr);
    while (stmt != nullptr && sqlite3_step(stmt) == SQLITE_ROW)
    {
        db.meta[column(stmt, 0)] = column(stmt, 1);
    }
    sqlite3_finalize(stmt);
    sqlite3_prepare_v2(con, "SELECT type, name, sql FROM sqlite_master ORDER BY name", -1, &stmt, nullptr);
    while (stmt != nullptr && sqlite3_step(stmt) == SQLITE_ROW)
    {
        db.schema.push_back(column(stmt, 0) + "|" + column(stmt, 1) + "|" + column(stmt, 2));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(con);
    return db;
}

std::string metaOr(const Db &db, const std::string &key)
{
    auto it = db.meta.find(key);
    return it != db.meta.end() ? it->second : "<absent>";
}

// ---- checks that need no cache ----------------------------------------------

void checkArguments(nxt_cache *cache, const fs::path &tmp)
{
    std::printf("argument validation\n");
    const fs::path out = tmp / "args.sqlite";
    const std::string outS = utf8(out);
    int32_t v = 12345;
    check(nxt_gameval_index_version(nullptr, &v) == NXT_ERR_INVALID && v == 0,
          "probe: NULL handle -> NXT_ERR_INVALID, *out_version = 0");
    check(nxt_gameval_index_version(cache, nullptr) == NXT_ERR_INVALID, "probe: NULL out -> NXT_ERR_INVALID");
    check(nxt_build_gameval_db(nullptr, outS.c_str(), nullptr) == NXT_ERR_INVALID, "build: NULL handle");
    check(nxt_build_gameval_db(cache, nullptr, nullptr) == NXT_ERR_INVALID, "build: NULL out_path");
    check(nxt_build_gameval_db(cache, "", nullptr) == NXT_ERR_INVALID, "build: empty out_path");
    const char *badMeta[] = {
        "{not json", "[\"a\"]", "\"s\"", "{\"note\":1}", "{\"note\":null}",
        "{\"index_version\":\"1\"}", "{\"built\":\"x\"}", "{\"generator\":\"x\"}",
    };
    for (const char *m : badMeta)
    {
        const nxt_result rc = nxt_build_gameval_db(cache, outS.c_str(), m);
        check(rc == NXT_ERR_INVALID && !isPresent(out) && !isPresent(partialOf(out)),
              std::string("build: extra_meta_json ") + m + " -> NXT_ERR_INVALID, no file (" +
                  nxt_last_error() + ")");
    }
}

void checkAbsentIndex(const fs::path &tmp)
{
    std::printf("index 67 absent (empty cache directory)\n");
    const fs::path emptyDir = tmp / "empty-cache";
    fs::create_directories(emptyDir);
    nxt_cache *cache = nxt_cache_open_local(utf8(emptyDir).c_str());
    check(cache != nullptr, "open_local on an empty directory");
    if (cache == nullptr)
    {
        return;
    }
    checkArguments(cache, tmp);
    int32_t v = 12345;
    const nxt_result probe = nxt_gameval_index_version(cache, &v);
    check(probe == NXT_ERR_NOT_FOUND && v == 0,
          "probe -> NXT_ERR_NOT_FOUND (got " + std::to_string(probe) + "), *out_version = 0 (got " +
              std::to_string(v) + ")");
    const fs::path out = tmp / "absent.sqlite";
    const nxt_result build = nxt_build_gameval_db(cache, utf8(out).c_str(), nullptr);
    check(build == NXT_ERR_NOT_FOUND, "build -> NXT_ERR_NOT_FOUND (got " + std::to_string(build) + ": " +
                                          nxt_last_error() + ")");
    check(!isPresent(out) && !isPresent(partialOf(out)), "build wrote no file and no .partial");
    // A valid extra meta must not turn NOT_FOUND into anything else.
    check(nxt_build_gameval_db(cache, utf8(out).c_str(), "{\"note\":\"x\"}") == NXT_ERR_NOT_FOUND,
          "build with valid extra meta -> still NXT_ERR_NOT_FOUND");
    nxt_cache_close(cache);
    check(fs::is_empty(emptyDir), "the cache directory is still empty (no js5-67.jcache created)");
}

// ---- checks against the real cache ------------------------------------------

void checkAgainstDumper(const fs::path &dumper, const std::string &cacheDir,
                        const fs::path &tmp, const Db &mine, int32_t probe)
{
    std::printf("row identity with nxtcache-dumper --type gameval --sqlite\n");
    const fs::path theirs = tmp / "dumper.sqlite";
    const int rc = run(dumper, {"--cache", cacheDir, "--type", "gameval", "--sqlite", utf8(theirs)});
    check(rc == 0, "dumper exit code 0 (got " + std::to_string(rc) + ")");
    const Db ref = readDb(theirs);
    check(ref.isOpen && !ref.rows.empty(), "dumper database opens and has " + std::to_string(ref.rows.size()) + " rows");
    check(mine.rows.size() == ref.rows.size(), "row counts equal: export " + std::to_string(mine.rows.size()) +
                                                    ", dumper " + std::to_string(ref.rows.size()));
    size_t firstDiff = 0;
    while (firstDiff < mine.rows.size() && firstDiff < ref.rows.size() && mine.rows[firstDiff] == ref.rows[firstDiff])
    {
        firstDiff++;
    }
    check(mine.rows == ref.rows, "every (etype, eid, name) row equal (first difference at row " +
                                     std::to_string(firstDiff) + ")");
    check(mine.schema == ref.schema, "sqlite_master equal (" + std::to_string(mine.schema.size()) + " objects)");
    std::map<std::string, std::string> a = mine.meta;
    std::map<std::string, std::string> b = ref.meta;
    for (const char *k : {"generator", "built", "host_note"})
    {
        a.erase(k);
        b.erase(k);
    }
    check(a == b, "meta equal except generator/built/host_note (" + std::to_string(a.size()) + " keys)");
    for (const auto &[k, v] : a)
    {
        if (b.count(k) == 0 || b[k] != v)
        {
            std::printf("        meta %s: export '%s', dumper '%s'\n", k.c_str(), v.c_str(), metaOr(ref, k).c_str());
        }
    }
    check(metaOr(mine, "generator") == "nxt_build_gameval_db", "export meta.generator = nxt_build_gameval_db");
    check(metaOr(ref, "index_version") == std::to_string(probe),
          "probe " + std::to_string(probe) + " == dumper meta.index_version " + metaOr(ref, "index_version"));
}

void checkReplaceAndFailures(nxt_cache *cache, const fs::path &tmp, const Db &mine)
{
    std::printf("existing output, held-open output, unwritable output\n");
    const fs::path junk = tmp / "replace.sqlite";
    { std::ofstream(junk, std::ios::binary) << "not a database"; }
    check(nxt_build_gameval_db(cache, utf8(junk).c_str(), nullptr) == NXT_OK, "build over an existing file -> NXT_OK");
    check(readDb(junk).rows == mine.rows && !isPresent(partialOf(junk)), "the existing file was replaced by the full DB");

    const fs::path held = tmp / "held.sqlite";
    check(nxt_build_gameval_db(cache, utf8(held).c_str(), nullptr) == NXT_OK, "build held.sqlite");
    const std::vector<char> before = fileBytes(held);
    sqlite3 *con = nullptr;
    sqlite3_open_v2(utf8(held).c_str(), &con, SQLITE_OPEN_READONLY, nullptr);
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(con, "SELECT count(*) FROM gameval", -1, &stmt, nullptr);
    sqlite3_step(stmt);  // stay mid-read: the file is open and share-locked
    const nxt_result heldRc = nxt_build_gameval_db(cache, utf8(held).c_str(), nullptr);
#ifdef _WIN32
    check(heldRc == NXT_ERR_IO, "rebuild while a sqlite reader holds it -> NXT_ERR_IO (got " +
                                    std::to_string(heldRc) + ": " + nxt_last_error() + ")");
    check(fileBytes(held) == before, "the held file is byte-for-byte unchanged");
#else
    check(heldRc == NXT_OK, "rebuild while a reader holds it -> NXT_OK (POSIX rename)");
#endif
    check(!isPresent(partialOf(held)), "no .partial left after the held-open rebuild");
    sqlite3_finalize(stmt);
    sqlite3_close(con);

    const fs::path noDir = tmp / "no-such-dir" / "gameval.sqlite";
    const nxt_result noDirRc = nxt_build_gameval_db(cache, utf8(noDir).c_str(), nullptr);
    check(noDirRc == NXT_ERR_IO, "out_path in a missing directory -> NXT_ERR_IO (got " +
                                     std::to_string(noDirRc) + ": " + nxt_last_error() + ")");
    check(!isPresent(tmp / "no-such-dir"), "the missing directory was not created");

    const fs::path isDir = tmp / "is-a-dir";
    fs::create_directories(isDir);
    const nxt_result isDirRc = nxt_build_gameval_db(cache, utf8(isDir).c_str(), nullptr);
    check(isDirRc == NXT_ERR_IO, "out_path is a directory -> NXT_ERR_IO (got " + std::to_string(isDirRc) + ": " +
                                     nxt_last_error() + ")");
    check(fs::is_directory(isDir) && fs::is_empty(isDir) && !isPresent(partialOf(isDir)),
          "the directory is untouched and its .partial was removed");
}

std::string cachePathFrom(int argc, char **argv)
{
    if (argc > 2)
    {
        return argv[2];
    }
    if (const char *env = std::getenv("NXTCACHE_TEST_CACHE"))
    {
        return env;
    }
    return R"(C:\ProgramData\Jagex\RuneScape)";
}

fs::path makeTempDir()
{
    std::random_device rd;
    const fs::path dir = fs::temp_directory_path() / ("nxtcache-gvc-" + std::to_string(rd()));
    fs::create_directories(dir);
    return dir;
}

int finish(const fs::path &tmp, int skipCode)
{
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("\n%s: %d failure(s)\n", failures == 0 ? (skipCode != 0 ? "SKIP" : "PASS") : "FAIL", failures);
    if (failures != 0)
    {
        return 1;
    }
    return skipCode;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <nxtcache-dumper> [cache-dir]\n", argv[0]);
        return 2;
    }
    const fs::path dumper = argv[1];
    const std::string cacheDir = cachePathFrom(argc, argv);
    const fs::path tmp = makeTempDir();

    checkAbsentIndex(tmp);

    if (!isPresent(fs::path(cacheDir) / "js5-67.jcache"))
    {
        std::printf("\nno js5-67.jcache in %s: cache checks skipped\n", cacheDir.c_str());
        return finish(tmp, kExitSkip);
    }
    std::printf("cache %s\n", cacheDir.c_str());
    nxt_cache *cache = nxt_cache_open_local(cacheDir.c_str());
    check(cache != nullptr, "open_local");
    if (cache == nullptr)
    {
        return finish(tmp, 0);
    }

    std::printf("build through the export\n");
    int32_t probe = 0;
    const nxt_result probeRc = nxt_gameval_index_version(cache, &probe);
    check(probeRc == NXT_OK, "probe -> NXT_OK (got " + std::to_string(probeRc) + "), version " + std::to_string(probe));
    // Non-ASCII path and meta: the ABI says UTF-8 for both.
    const fs::path mineFile = tmp / u8"gämeval.sqlite";
    const nxt_result buildRc =
        nxt_build_gameval_db(cache, utf8(mineFile).c_str(), "{\"host_note\":\"h\xC3\xA9llo\"}");
    check(buildRc == NXT_OK,
          "build -> NXT_OK" + (buildRc == NXT_OK ? std::string() : " (" + std::string(nxt_last_error()) + ")"));
    check(isPresent(mineFile) && !isPresent(partialOf(mineFile)), "the DB is at the UTF-8 out_path, no .partial left");
    const Db mine = readDb(mineFile);
    check(mine.isOpen && !mine.rows.empty(), "export database opens and has " + std::to_string(mine.rows.size()) + " rows");
    check(metaOr(mine, "index_version") == std::to_string(probe),
          "probe " + std::to_string(probe) + " == meta.index_version " + metaOr(mine, "index_version"));
    check(metaOr(mine, "host_note") == "h\xC3\xA9llo", "extra meta row written verbatim (UTF-8)");
    check(metaOr(mine, "schema_version") == "1" && metaOr(mine, "source") == "local",
          "meta.schema_version = 1, meta.source = local");
    check(metaOr(mine, "rows") == std::to_string(mine.rows.size()), "meta.rows matches the row count");

    // The build evicts each archive it decodes; the handle must still serve them.
    char *locJson = nullptr;
    size_t locLen = 0;
    const nxt_result locRc = nxt_get_gameval_group_json(cache, "loc", &locJson, &locLen);
    check(locRc == NXT_OK && locLen > 0, "same handle after the build: nxt_get_gameval_group_json(loc) -> NXT_OK (got " +
                                             std::to_string(locRc) + ")");
    nxt_free(locJson);
    int32_t probeAfter = 0;
    check(nxt_gameval_index_version(cache, &probeAfter) == NXT_OK && probeAfter == probe,
          "same handle after the build: the probe is unchanged");

    checkAgainstDumper(dumper, cacheDir, tmp, mine, probe);
    checkReplaceAndFailures(cache, tmp, mine);  // rebuilds on the same handle

    nxt_cache_close(cache);
    return finish(tmp, 0);
}
