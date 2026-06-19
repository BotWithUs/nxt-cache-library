#pragma once

#include "core/Archive.h"

#include <vector>

class CacheSource
{
public:
    virtual ~CacheSource() = default;

    virtual std::vector<int> archiveIds(int indexId) = 0;
    virtual Archive &archive(int indexId, int archiveId) = 0;

    // Drop a cached archive (data buffer + per-file map). Used by bulk
    // dumpers that sweep every archive once and would otherwise let the
    // backing index accumulate gigabytes of decompressed data.
    // Default: no-op (live caches don't memoize archives).
    virtual void evictArchive(int /*indexId*/, int /*archiveId*/) {}
};
