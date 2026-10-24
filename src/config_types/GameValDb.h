#pragma once

#include "core/CacheSource.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

// Decode every index-67 archive of `cache`, verify the bit-table split against
// the varbit configs (index 2 archive 69) when the cache has them, and write
// the database to `outFile`. `extraMeta` rows (source, cache, build, ...) are
// added to meta beside schema_version/built/groups/rows/bit_table_check.
//
// All-or-nothing: the file is written beside `outFile` and renamed over it only
// once complete, so a failure never leaves a partial or half-replaced index.
// Throws std::runtime_error when index 67 has no archives, an archive is absent
// or decodes to nothing, a bit table maps onto varbits of another domain, or
// sqlite fails.
GameValDbResult buildGameValDb(CacheSource &cache, const std::filesystem::path &outFile,
                               const std::vector<std::pair<std::string, std::string>> &extraMeta);

}  // namespace nxt
