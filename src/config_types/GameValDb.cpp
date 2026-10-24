#include "config_types/GameValDb.h"

#include "config_types/GameVal.h"
#include "config_types/Types.h"

#include <sqlite3.h>

#include <ctime>
#include <map>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace nxt {

namespace {

constexpr int kConfigIndex = 2;
constexpr int kVarbitArchive = 69;

// One table as it lands in the database: an etype and its rows, sorted by id.
struct EtypeTable
{
    std::string etype;
    int archive = -1;
    std::vector<GameValEntry> entries;
    const GameValBitTable *bitSpec = nullptr;  // set on a split-out bit table
};

std::vector<GameValEntry> readGroup(CacheSource &cache, int archiveId)
{
    Archive &archive = cache.archive(kGameValIndex, archiveId);
    if (archive.id == -1)
    {
        throw std::runtime_error("gameval archive " + std::to_string(archiveId) +
                                 " is listed by index 67 but absent");
    }
    RSBuffer buffer = archive.readFile(0);
    if (buffer.buffer == nullptr || buffer.remaining() == 0)
    {
        throw std::runtime_error("gameval archive " + std::to_string(archiveId) +
                                 " has no table file");
    }
    std::vector<GameValEntry> entries = decodeGameVals(buffer);
    if (entries.empty())
    {
        // Silently dropping a group would leave the host resolving a whole
        // namespace to "not found".
        throw std::runtime_error("gameval archive " + std::to_string(archiveId) +
                                 " decoded to no entries (bad format or truncated)");
    }
    return entries;
}

// Decode every index-67 archive into etype tables; var_* archives that pack a
// bit table contribute two.
std::vector<EtypeTable> collectTables(CacheSource &cache, GameValDbResult &result)
{
    std::vector<int> archiveIds = cache.archiveIds(kGameValIndex);
    if (archiveIds.empty())
    {
        throw std::runtime_error("cache has no gameval archives (index 67 absent or empty)");
    }
    std::vector<EtypeTable> tables;
    for (int aid : archiveIds)
    {
        std::vector<GameValEntry> entries = readGroup(cache, aid);
        cache.evictArchive(kGameValIndex, aid);
        std::string group = gameValGroupName(aid);
        if (group.empty())
        {
            group = "archive_" + std::to_string(aid);
            result.warnings.push_back("gameval archive " + std::to_string(aid) +
                                      " is not in gameValGroups(); stored as etype " + group);
        }
        const GameValBitTable *spec = gameValBitTable(aid);
        if (spec == nullptr)
        {
            tables.push_back(EtypeTable{gameValEtype(group), aid, std::move(entries), nullptr});
            continue;
        }
        GameValSplit split = splitGameValBitTable(entries);
        tables.push_back(EtypeTable{gameValEtype(group), aid, std::move(split.vars), nullptr});
        if (!split.isSplit)
        {
            result.warnings.push_back(group + ": no packed " + spec->bitEtype + " block found");
            continue;
        }
        tables.push_back(EtypeTable{spec->bitEtype, aid, std::move(split.bits), spec});
    }
    return tables;
}

// varbit id -> domainType from index 2 archive 69; empty if the cache lacks it.
std::unordered_map<int, int> readVarbitDomains(CacheSource &cache)
{
    std::unordered_map<int, int> out;
    Archive &archive = cache.archive(kConfigIndex, kVarbitArchive);
    if (archive.id == -1)
    {
        return out;
    }
    for (int fid : archive.fileIds)
    {
        RSBuffer buffer = archive.readFile(fid);
        if (buffer.buffer == nullptr || buffer.remaining() == 0)
        {
            continue;
        }
        VarbitType varbit;
        varbit.id = fid;
        varbit.decode(buffer);
        out[fid] = varbit.domainType;
    }
    return out;
}

// The split's base is an inference, so check it against ground truth: every bit
// must be a varbit of its table's domain. A wrong base lands bits on varbits of
// other domains, which is an error; a bit with no varbit config at all (a name
// that outlived its config) is only a warning.
void verifyBitTables(CacheSource &cache, const std::vector<EtypeTable> &tables,
                     GameValDbResult &result)
{
    const std::unordered_map<int, int> domains = readVarbitDomains(cache);
    if (domains.empty())
    {
        result.bitTableCheck = "skipped: no varbit configs (index 2 archive 69) in this cache";
        return;
    }
    size_t checked = 0;
    for (const EtypeTable &t : tables)
    {
        if (t.bitSpec == nullptr)
        {
            continue;
        }
        size_t wrongDomain = 0;
        size_t missing = 0;
        for (const GameValEntry &e : t.entries)
        {
            auto it = domains.find(e.id);
            if (it == domains.end())
            {
                missing++;
            }
            else if (it->second != t.bitSpec->varbitDomain)
            {
                wrongDomain++;
            }
        }
        if (wrongDomain != 0)
        {
            throw std::runtime_error(t.etype + ": " + std::to_string(wrongDomain) + " of " +
                                     std::to_string(t.entries.size()) +
                                     " bits are varbits of another domain (bit-table base is wrong)");
        }
        if (missing != 0)
        {
            result.warnings.push_back(t.etype + ": " + std::to_string(missing) +
                                      " names have no varbit config");
        }
        checked += t.entries.size();
    }
    result.bitTableCheck = "verified: " + std::to_string(checked) +
                           " bits are varbits of their table's domain";
}

struct DbCloser
{
    void operator()(sqlite3 *db) const { sqlite3_close(db); }
};
struct StmtFinalizer
{
    void operator()(sqlite3_stmt *stmt) const { sqlite3_finalize(stmt); }
};
using DbPtr = std::unique_ptr<sqlite3, DbCloser>;
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtFinalizer>;

void exec(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK)
    {
        std::string msg = err != nullptr ? err : sqlite3_errmsg(db);
        sqlite3_free(err);
        throw std::runtime_error(std::string("sqlite: ") + msg + " in: " + sql);
    }
}

StmtPtr prepare(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        throw std::runtime_error(std::string("sqlite prepare: ") + sqlite3_errmsg(db));
    }
    return StmtPtr(stmt);
}

void insertPair(sqlite3 *db, sqlite3_stmt *stmt, const std::string &a, const std::string &b)
{
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, a.c_str(), static_cast<int>(a.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, b.c_str(), static_cast<int>(b.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("sqlite insert: ") + sqlite3_errmsg(db));
    }
}

GameValDbTypeStats insertTable(sqlite3 *db, sqlite3_stmt *stmt, const EtypeTable &t)
{
    GameValDbTypeStats stats{t.etype, t.archive, t.entries.size(), 0};
    std::unordered_map<std::string, int> seen;
    for (const GameValEntry &e : t.entries)
    {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, t.etype.c_str(), static_cast<int>(t.etype.size()), SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, e.id);
        sqlite3_bind_text(stmt, 3, e.name.c_str(), static_cast<int>(e.name.size()), SQLITE_STATIC);
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            throw std::runtime_error("sqlite insert " + t.etype + "/" + std::to_string(e.id) +
                                     ": " + sqlite3_errmsg(db));
        }
        stats.duplicateNames += seen.emplace(e.name, e.id).second ? 0 : 1;
    }
    return stats;
}

std::string localTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

constexpr const char *kSchemaSql = R"(
CREATE TABLE gameval (
    etype TEXT    NOT NULL,
    eid   INTEGER NOT NULL,
    name  TEXT    NOT NULL,
    PRIMARY KEY (etype, eid)
) WITHOUT ROWID;
CREATE INDEX idx_gameval_lookup ON gameval(etype, name);
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
)";

void writeDb(const std::filesystem::path &file, const std::vector<EtypeTable> &tables,
             const std::vector<std::pair<std::string, std::string>> &meta, GameValDbResult &result)
{
    sqlite3 *raw = nullptr;
    const std::u8string u8 = file.u8string();
    const int rc = sqlite3_open_v2(reinterpret_cast<const char *>(u8.c_str()), &raw,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    DbPtr db(raw);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("sqlite open " + file.string() + ": " +
                                 (raw != nullptr ? sqlite3_errmsg(raw) : "out of memory"));
    }
    // A scratch file renamed into place only when complete: no journal needed.
    exec(db.get(), "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;");
    exec(db.get(), kSchemaSql);
    exec(db.get(), "BEGIN");
    {
        StmtPtr row = prepare(db.get(), "INSERT INTO gameval (etype, eid, name) VALUES (?, ?, ?)");
        for (const EtypeTable &t : tables)
        {
            result.types.push_back(insertTable(db.get(), row.get(), t));
            result.rows += t.entries.size();
        }
        StmtPtr kv = prepare(db.get(), "INSERT INTO meta (key, value) VALUES (?, ?)");
        for (const auto &[key, value] : meta)
        {
            insertPair(db.get(), kv.get(), key, value);
        }
        insertPair(db.get(), kv.get(), "groups", std::to_string(tables.size()));
        insertPair(db.get(), kv.get(), "rows", std::to_string(result.rows));
        insertPair(db.get(), kv.get(), "bit_table_check", result.bitTableCheck);
    }
    exec(db.get(), "COMMIT");
    exec(db.get(), "ANALYZE");
    exec(db.get(), "VACUUM");
}

// Removes the scratch file unless released; covers every throw after creation.
struct ScratchFile
{
    std::filesystem::path path;
    bool isReleased = false;

    ~ScratchFile()
    {
        if (!isReleased)
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

}  // namespace

GameValDbResult buildGameValDb(CacheSource &cache, const std::filesystem::path &outFile,
                               const std::vector<std::pair<std::string, std::string>> &extraMeta)
{
    GameValDbResult result;
    std::vector<EtypeTable> tables = collectTables(cache, result);
    verifyBitTables(cache, tables, result);

    std::vector<std::pair<std::string, std::string>> meta = {
        {"schema_version", kGameValDbSchemaVersion},
        {"built", localTimestamp()},
    };
    for (const auto &kv : extraMeta)
    {
        const bool isReserved = kv.first == "schema_version" || kv.first == "built" ||
                                kv.first == "groups" || kv.first == "rows" ||
                                kv.first == "bit_table_check";
        if (isReserved)
        {
            throw std::runtime_error("meta key '" + kv.first + "' is written by the builder");
        }
        meta.push_back(kv);
    }

    ScratchFile scratch{outFile.string() + ".partial"};
    std::filesystem::remove(scratch.path);
    writeDb(scratch.path, tables, meta, result);
    std::filesystem::rename(scratch.path, outFile);
    scratch.isReleased = true;
    return result;
}

}  // namespace nxt
