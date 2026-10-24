#pragma once

#include "core/CacheSource.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace js5 {
struct ServerConfig;
}

// Bakes the gameval name index (gameval.sqlite) the Java host reads through
// core/gameval/SqliteGamevalIndex. Replaces GameVal Information/
// build_gameval_db.py + fetch_gameval_beta.py: same schema v1, same two etype
// renames (obj -> item, var_player -> varp), same five packed bit tables split
// out of the var_* groups -- but with the bit tables re-based correctly (see
// splitGameValBitTable) and read from any cache source (local, live, beta).
//
// Schema v1, a shared surface with the host -- do not change it here without
// bumping kGameValDbSchemaVersion in lockstep with the host's reader:
//
//   CREATE TABLE gameval (etype TEXT NOT NULL, eid INTEGER NOT NULL,
//                         name TEXT NOT NULL, PRIMARY KEY (etype, eid)) WITHOUT ROWID;
//   CREATE INDEX idx_gameval_lookup ON gameval(etype, name);
//   CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
namespace nxt {

inline constexpr const char *kGameValDbSchemaVersion = "1";

struct GameValDbTypeStats
{
    std::string etype;
    int archive = -1;
    size_t rows = 0;
    size_t duplicateNames = 0;  // names repeated within the etype (lookup picks one)
};

struct GameValDbResult
{
    std::vector<GameValDbTypeStats> types;  // in archive order, bit tables after their var table
    size_t rows = 0;
    std::string bitTableCheck;              // "verified ..." or "skipped: <why>"
    std::vector<std::string> warnings;
};

// A failure writing the database (sqlite, or creating/renaming the file), as
// opposed to a cache that cannot be read or decoded (plain std::runtime_error).
class GameValDbIoError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Index 67's reference-table version: the value written as meta.index_version
// and the closest thing a cache has to a gameval revision stamp. nullopt when
// index 67 lists no archives (no js5-67.jcache, an empty one, or a cache from
// before 951-1 with no live fallback to fill it) -- exactly the case in which
// buildGameValDb refuses. Loads only the reference table, never an archive.
std::optional<int> gameValIndexVersion(CacheSource &cache);

// Where a database's gamevals come from, for its meta rows.
struct GameValSourceInfo
{
    std::string generator;                          // meta.generator
    const js5::ServerConfig *liveConfig = nullptr;  // non-null: `cache` is a live JS5 source
    bool isBeta = false;                            // the beta endpoint (live) or fallback (local)
    bool hasFallback = false;                       // local cache backed by a live JS5 fallback
    std::filesystem::path cacheDir;                 // local cache directory (ignored for live)
};

// meta rows naming the source, in this order:
//   generator; source = local | local+live-fallback | local+beta-fallback | live | beta;
//   cache = the absolute directory or the JS5 endpoint; build = the JS5 server
//   build (live only); index_version = gameValIndexVersion(cache);
//   source_mtime = js5-67.jcache's mtime (local only, when the file exists).
// Throws std::runtime_error when index 67 is absent (see gameValIndexVersion).
std::vector<std::pair<std::string, std::string>> gameValSourceMeta(CacheSource &cache,
                                                                   const GameValSourceInfo &info);

// True for a meta key that buildGameValDb or gameValSourceMeta writes, which a
// caller's extra meta must therefore not supply.
bool isGameValMetaKeyReserved(std::string_view key);

// Decode every index-67 archive of `cache`, verify the bit-table split against
// the varbit configs (index 2 archive 69) when the cache has them, and write
// the database to `outFile`. `extraMeta` rows (source, cache, build, ...) are
// added to meta beside schema_version/built/groups/rows/bit_table_check.
//
// All-or-nothing: the file is written beside `outFile` and renamed over it only
// once complete, so a failure never leaves a partial or half-replaced index.
// An existing `outFile` is replaced by the rename (atomically, on one volume);
// on failure it is left untouched and the scratch file is removed.
// Throws std::runtime_error when index 67 has no archives, an archive is absent
// or decodes to nothing, or a bit table maps onto varbits of another domain;
// GameValDbIoError when sqlite or the filesystem fails.
GameValDbResult buildGameValDb(CacheSource &cache, const std::filesystem::path &outFile,
                               const std::vector<std::pair<std::string, std::string>> &extraMeta);

}  // namespace nxt
