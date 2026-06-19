#pragma once

#include "core/Archive.h"

#include <vector>

class CacheSource
{
public:
    virtual ~CacheSource() = default;

    virtual std::vector<int> archiveIds(int indexId) = 0;
    virtual Archive &archive(int indexId, int archiveId) = 0;

    // Return the file ids the index reference table declares for one archive,
    // in ascending order, WITHOUT decoding the archive's file contents — empty
    // if the archive is absent. Both backends populate the per-archive file id
    // set when the (already-loaded) reference table is decoded, so subclasses
    // override this with a path that never touches the data blob. The default
    // materializes the archive (still correct, just not for free) so a new
    // backend works before it supplies a cheap override.
    virtual std::vector<int> fileIds(int indexId, int archiveId)
    {
        Archive &a = archive(indexId, archiveId);
        if (a.id == -1) return {};
        return std::vector<int>(a.fileIds.begin(), a.fileIds.end());
    }

    // Drop a cached archive (data buffer + per-file map). Used by bulk
    // dumpers that sweep every archive once and would otherwise let the
    // backing index accumulate gigabytes of decompressed data.
    // Default: no-op (live caches don't memoize archives).
    virtual void evictArchive(int /*indexId*/, int /*archiveId*/) {}
};
