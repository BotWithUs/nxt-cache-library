#pragma once

#include "core/Archive.h"

#include <vector>

class CacheSource
{
public:
    virtual ~CacheSource() = default;

    virtual std::vector<int> archiveIds(int indexId) = 0;
    virtual Archive &archive(int indexId, int archiveId) = 0;
};
