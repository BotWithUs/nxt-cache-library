#pragma once

#include "core/Index.h"

#include <map>
#include <string>
#include <utility>

class RSCache
{
    std::map<int, Index> indices;
public:
    std::string path;

    explicit RSCache(std::string path) : path(std::move(path)) {}

    Index &index(int id);

    FileHeader &file(int indexId, int archiveId, int fileId);
};
