// nxtcache-mirror — byte-faithful JS5 cache mirror.
//
// Downloads the full RuneScape NXT cache from the LIVE game over the JS5 binary
// protocol and writes it to local `.jcache` sqlite files (one per index, named
// `js5-<N>.jcache`) that a separate Java server serves verbatim. Every blob is
// stored as the RAW JS5 container exactly as it came off the wire — never
// decompressed, decoded, or re-packed — because the downstream server is a
// pass-through.
//
// Per-index sqlite layout (the contract the Java reader expects):
//   cache       (key INTEGER PRIMARY KEY, data BLOB)  -- archive containers
//   cache_index (key INTEGER PRIMARY KEY, data BLOB)  -- this index's ref table
//
// Storage convention:
//   (255,255) master       -> js5-255.jcache, cache_index, key=255
//   (255,N)   ref table    -> js5-N.jcache,   cache_index, key=N
//   (N,A)     archive       -> js5-N.jcache,   cache,       key=A
//
// Resumable: an archive already present in `cache` is skipped, so a re-run
// continues where it left off. Robust: a getFile network hiccup reconnects and
// retries before giving up on that file. Transactional: archive inserts are
// committed in batches per index for speed and crash-safety.

#include "network/Js5Compression.h"
#include "network/Js5Config.h"
#include "network/Js5Index.h"
#include "network/Js5Socket.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr int kJs5Master = 255;          // master/checksum table lives at (255,255)
constexpr int kMaxRetries = 5;           // getFile attempts before a file is abandoned
constexpr int kReconnectDelayMs = 1000;  // pause before reconnecting after a failure
constexpr int kCommitBatch = 500;        // archive inserts per BEGIN/COMMIT window
constexpr int kProgressEvery = 250;      // log progress every N processed archives
constexpr const char *kDefaultOutDir = "E:/Projects/RevonX3/cache";

struct MirrorOptions
{
    std::string outDir = kDefaultOutDir;
    std::vector<int> indices;  // empty => mirror every index the master advertises
    bool beta = false;
};

struct MirrorStats
{
    long downloaded = 0;
    long skipped = 0;
    long failed = 0;
};

enum class ParseResult
{
    Run,
    Help,
    Error
};

// --- sqlite helpers ---------------------------------------------------------

[[noreturn]] void throwSqlite(sqlite3 *db, const std::string &what)
{
    throw std::runtime_error(what + ": " + (db ? sqlite3_errmsg(db) : "out of memory"));
}

sqlite3 *openDb(const std::filesystem::path &path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK)
    {
        std::string msg = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        throw std::runtime_error("cannot open " + path.string() + ": " + msg);
    }
    return db;
}

void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK)
    {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec failed (" + std::string(sql) + "): " + msg);
    }
}

void ensureSchema(sqlite3 *db)
{
    execSql(db, "CREATE TABLE IF NOT EXISTS cache (key INTEGER PRIMARY KEY, data BLOB)");
    execSql(db, "CREATE TABLE IF NOT EXISTS cache_index (key INTEGER PRIMARY KEY, data BLOB)");
    // Default rollback journal (crash-safe) with relaxed fsync for throughput.
    execSql(db, "PRAGMA synchronous=NORMAL");
}

// Returns true if `table` already holds a row with primary key `key`.
bool hasKey(sqlite3 *db, const char *table, int key)
{
    std::string sql = std::string("SELECT 1 FROM ") + table + " WHERE key = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        throwSqlite(db, "prepare hasKey");
    }
    sqlite3_bind_int(stmt, 1, key);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

// Inserts the raw container bytes verbatim under `key`. SQLITE_TRANSIENT makes
// sqlite copy the blob, so a temporary `data` is safe.
void insertBlob(sqlite3 *db, const char *table, int key, const std::vector<uint8_t> &data)
{
    std::string sql = std::string("INSERT OR REPLACE INTO ") + table + " (key, data) VALUES (?, ?)";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        throwSqlite(db, "prepare insertBlob");
    }
    sqlite3_bind_int(stmt, 1, key);
    sqlite3_bind_blob(stmt, 2, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        throwSqlite(db, std::string("insert into ") + table);
    }
}

// --- network helpers --------------------------------------------------------

// Fetches the raw JS5 container for (major, minor), reconnecting and retrying on
// any failure (network hiccup, peer reset). On the final attempt the error is
// rethrown. The socket is recreated lazily inside the try so a failed reconnect
// is itself retried within the same budget.
std::vector<uint8_t> getFileWithRetry(std::unique_ptr<js5::Js5Socket> &socket,
                                      const js5::ServerConfig &config, int major, int minor)
{
    for (int attempt = 0; attempt <= kMaxRetries; attempt++)
    {
        try
        {
            if (!socket)
            {
                socket = std::make_unique<js5::Js5Socket>(config);
            }
            return socket->getFile(major, minor);
        }
        catch (const std::exception &e)
        {
            socket.reset();
            if (attempt >= kMaxRetries)
            {
                throw;
            }
            std::cerr << "  getFile(" << major << "," << minor << ") failed (" << e.what()
                      << "); reconnecting " << (attempt + 1) << "/" << kMaxRetries << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
        }
    }
    throw std::runtime_error("getFileWithRetry: retries exhausted");
}

// Loads and decodes index `indexId`'s reference table into a Js5Index so its
// archive id list can be enumerated. Retries with reconnect like getFileWithRetry.
js5::Js5Index loadIndex(std::unique_ptr<js5::Js5Socket> &socket,
                        const js5::ServerConfig &config, int indexId)
{
    for (int attempt = 0; attempt <= kMaxRetries; attempt++)
    {
        try
        {
            if (!socket)
            {
                socket = std::make_unique<js5::Js5Socket>(config);
            }
            return js5::Js5Index(indexId, socket.get());
        }
        catch (const std::exception &e)
        {
            socket.reset();
            if (attempt >= kMaxRetries)
            {
                throw;
            }
            std::cerr << "  index " << indexId << " ref-table load failed (" << e.what()
                      << "); reconnecting " << (attempt + 1) << "/" << kMaxRetries << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
        }
    }
    throw std::runtime_error("loadIndex: retries exhausted");
}

// The master/checksum table (255,255) starts with a single byte count of the
// content indices that follow. Decompress and read it.
int parseMasterCount(const std::vector<uint8_t> &masterRaw)
{
    std::vector<uint8_t> bytes = js5::decompress(masterRaw.data(), masterRaw.size());
    if (bytes.empty())
    {
        throw std::runtime_error("master index decompressed to zero bytes");
    }
    return static_cast<int>(bytes[0]);
}

// --- mirror steps -----------------------------------------------------------

// Stores the raw master container into js5-255.jcache (cache_index, key=255) if
// absent, and returns the number of content indices it advertises.
int mirrorMasterAndCount(std::unique_ptr<js5::Js5Socket> &socket,
                         const js5::ServerConfig &config, const std::filesystem::path &outDir)
{
    std::vector<uint8_t> masterRaw = getFileWithRetry(socket, config, kJs5Master, kJs5Master);
    sqlite3 *db = openDb(outDir / "js5-255.jcache");
    ensureSchema(db);
    if (!hasKey(db, "cache_index", kJs5Master))
    {
        insertBlob(db, "cache_index", kJs5Master, masterRaw);
    }
    sqlite3_close(db);
    return parseMasterCount(masterRaw);
}

// Fetches and stores index `indexId`'s raw reference table (255,indexId) into its
// cache_index table if not already present.
void storeRefTableIfAbsent(sqlite3 *db, std::unique_ptr<js5::Js5Socket> &socket,
                           const js5::ServerConfig &config, int indexId)
{
    if (hasKey(db, "cache_index", indexId))
    {
        return;
    }
    std::vector<uint8_t> raw = getFileWithRetry(socket, config, kJs5Master, indexId);
    insertBlob(db, "cache_index", indexId, raw);
}

// Mirrors one archive (indexId,archiveId) into the `cache` table. Returns true if
// a new blob was inserted (so the caller can batch commits). Already-present
// archives are skipped; a permanently failing download is logged and counted.
bool mirrorOneArchive(sqlite3 *db, std::unique_ptr<js5::Js5Socket> &socket,
                      const js5::ServerConfig &config, int indexId, int archiveId,
                      MirrorStats &ioStats)
{
    if (hasKey(db, "cache", archiveId))
    {
        ioStats.skipped++;
        return false;
    }
    try
    {
        std::vector<uint8_t> raw = getFileWithRetry(socket, config, indexId, archiveId);
        insertBlob(db, "cache", archiveId, raw);
        ioStats.downloaded++;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "  [index " << indexId << "] archive " << archiveId
                  << " abandoned: " << e.what() << "\n";
        ioStats.failed++;
        return false;
    }
}

// Mirrors every archive of one index, batching inserts into transactions and
// logging progress. Commits per kCommitBatch new blobs and once at the end.
void mirrorArchives(sqlite3 *db, std::unique_ptr<js5::Js5Socket> &socket,
                    const js5::ServerConfig &config, int indexId,
                    const std::vector<int> &archiveIds, MirrorStats &ioStats)
{
    const int total = static_cast<int>(archiveIds.size());
    int processed = 0;
    int sinceCommit = 0;
    execSql(db, "BEGIN");
    for (int archiveId : archiveIds)
    {
        if (mirrorOneArchive(db, socket, config, indexId, archiveId, ioStats))
        {
            sinceCommit++;
        }
        processed++;
        if (sinceCommit >= kCommitBatch)
        {
            execSql(db, "COMMIT");
            execSql(db, "BEGIN");
            sinceCommit = 0;
        }
        if (processed % kProgressEvery == 0 || processed == total)
        {
            std::cout << "  index " << indexId << ": " << processed << "/" << total
                      << " archives (down=" << ioStats.downloaded << " skip=" << ioStats.skipped
                      << " fail=" << ioStats.failed << ")\n";
        }
    }
    execSql(db, "COMMIT");
}

// Mirrors a single content index: its reference table plus every archive it lists.
void mirrorOneIndex(std::unique_ptr<js5::Js5Socket> &socket, const js5::ServerConfig &config,
                    const std::filesystem::path &outDir, int indexId, MirrorStats &ioStats)
{
    std::filesystem::path path = outDir / ("js5-" + std::to_string(indexId) + ".jcache");
    sqlite3 *db = openDb(path);
    ensureSchema(db);

    storeRefTableIfAbsent(db, socket, config, indexId);
    std::vector<int> archiveIds = loadIndex(socket, config, indexId).archiveIds;
    std::cout << "index " << indexId << ": " << archiveIds.size() << " archives\n";

    mirrorArchives(db, socket, config, indexId, archiveIds, ioStats);
    sqlite3_close(db);
}

// --- CLI --------------------------------------------------------------------

void usage(const char *prog)
{
    std::cerr
        << "nxtcache-mirror — byte-faithful JS5 cache mirror to .jcache sqlite files\n\n"
        << "Usage: " << prog << " [--out <dir>] [--indices <a,b,c>] [--beta]\n\n"
        << "Options:\n"
        << "  --out <dir>        Output directory for js5-*.jcache (default: " << kDefaultOutDir
        << ")\n"
        << "  --indices <list>   Comma-separated index ids to mirror (default: all)\n"
        << "  --beta             Use the beta JS5 endpoint/config\n"
        << "  --help             Show this message\n\n"
        << "Always mirrors the master (255,255) and each index's reference table.\n"
        << "Resumable: existing archives are skipped, so re-running is safe.\n";
}

bool parseIntList(const std::string &s, std::vector<int> &out)
{
    std::size_t start = 0;
    while (start <= s.size())
    {
        std::size_t comma = s.find(',', start);
        std::string tok = s.substr(start, comma == std::string::npos ? std::string::npos
                                                                      : comma - start);
        if (!tok.empty())
        {
            try
            {
                std::size_t end = 0;
                int v = std::stoi(tok, &end, 0);
                if (end != tok.size())
                {
                    return false;
                }
                out.push_back(v);
            }
            catch (...)
            {
                return false;
            }
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return true;
}

ParseResult parseArgs(int argc, char **argv, MirrorOptions &outOpts)
{
    for (int i = 1; i < argc; i++)
    {
        std::string_view a = argv[i];
        if (a == "--help" || a == "-h")
        {
            usage(argv[0]);
            return ParseResult::Help;
        }
        else if (a == "--beta")
        {
            outOpts.beta = true;
        }
        else if (a == "--out" || a == "--indices")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing argument after " << a << "\n";
                return ParseResult::Error;
            }
            std::string value = argv[++i];
            if (a == "--out")
            {
                outOpts.outDir = value;
            }
            else if (!parseIntList(value, outOpts.indices))
            {
                std::cerr << "Invalid --indices list: " << value << "\n";
                return ParseResult::Error;
            }
        }
        else
        {
            std::cerr << "Unknown argument: " << a << "\n\n";
            usage(argv[0]);
            return ParseResult::Error;
        }
    }
    return ParseResult::Run;
}

// Resolves the concrete index list: an explicit --indices selection, else every
// index id 0..indexCount-1 the master advertised.
std::vector<int> resolveIndexList(const MirrorOptions &opts, int indexCount)
{
    if (!opts.indices.empty())
    {
        return opts.indices;
    }
    std::vector<int> all;
    all.reserve(static_cast<std::size_t>(indexCount));
    for (int n = 0; n < indexCount; n++)
    {
        all.push_back(n);
    }
    return all;
}

void runMirror(const MirrorOptions &opts)
{
    std::filesystem::path outDir = opts.outDir;
    std::filesystem::create_directories(outDir);

    js5::ServerConfig config = opts.beta ? js5::fetchServerConfigBeta() : js5::fetchServerConfig();
    std::cout << "Connected to " << (opts.beta ? "BETA" : "live") << " JS5: build "
              << config.serverVersionMajor << " @ " << config.endpoint << ":" << config.port << "\n";

    auto socket = std::make_unique<js5::Js5Socket>(config);

    int indexCount = mirrorMasterAndCount(socket, config, outDir);
    std::cout << "master advertises " << indexCount << " content indices\n";

    std::vector<int> indices = resolveIndexList(opts, indexCount);
    std::cout << "mirroring " << indices.size() << " index(es) into " << outDir.string() << "\n";

    MirrorStats stats;
    for (int n : indices)
    {
        mirrorOneIndex(socket, config, outDir, n, stats);
    }
    std::cout << "DONE. downloaded=" << stats.downloaded << " skipped=" << stats.skipped
              << " failed=" << stats.failed << "\n";
}

}  // namespace

int main(int argc, char **argv)
{
    MirrorOptions opts;
    ParseResult pr = parseArgs(argc, argv, opts);
    if (pr == ParseResult::Help)
    {
        return 0;
    }
    if (pr == ParseResult::Error)
    {
        return 2;
    }

    try
    {
        runMirror(opts);
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
