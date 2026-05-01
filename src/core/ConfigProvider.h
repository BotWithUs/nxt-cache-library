#pragma once

#include "core/RSCache.h"

#include <cmath>
#include <map>

template<typename Type>
class ConfigProvider
{
    int indexId;
    int archiveId;
    int shift;
    RSCache *cache;
    std::map<int, Type> configs;
public:

    ConfigProvider(RSCache *cache, int index, int archive, int shift)
        : indexId(index), archiveId(archive), shift(shift), cache(cache)
    {
    }

    uint32_t getCapacity()
    {
        auto &index = cache->index(this->indexId);
        if (this->archiveId != -1)
        {
            auto &archive = index.archive(this->archiveId);
            return static_cast<uint32_t>(archive.files.size());
        }
        if (index.archives.empty())
        {
            return 0;
        }
        auto end = index.archives.end();
        --end;
        auto lastKey = end->first;
        auto &last = index.archive(lastKey);
        if (last.files.empty())
        {
            return 0;
        }
        auto endFile = last.files.end();
        --endFile;
        return static_cast<uint32_t>(lastKey * std::pow(2.0, shift) + endFile->first);
    }

    Type &load(int id)
    {
        auto &index = cache->index(this->indexId);
        int aid = this->archiveId;
        int file = id;
        if (aid == -1)
        {
            aid = id >> shift;
            file = id & ((1 << shift) - 1);
        }
        auto &archive = index.archive(aid);
        auto buffer = archive.readFile(file);
        Type type{};
        type.id = id;
        if (buffer.buffer != nullptr && buffer.remaining() > 0)
        {
            type.decode(buffer);
        }
        configs[id] = std::move(type);
        return configs[id];
    }

    Type &provide(int id)
    {
        auto it = configs.find(id);
        if (it != configs.end())
        {
            return it->second;
        }
        return load(id);
    }

    void preload()
    {
        uint32_t cap = getCapacity();
        for (uint32_t i = 0; i < cap; i++)
        {
            int key = static_cast<int>(i);
            if (configs.contains(key))
            {
                continue;
            }
            auto &index = cache->index(this->indexId);
            int aid = this->archiveId;
            int file = key;
            if (aid == -1)
            {
                aid = key >> shift;
                file = key & ((1 << shift) - 1);
            }
            auto &archive = index.archive(aid);
            auto buffer = archive.readFile(file);
            if (buffer.buffer != nullptr && buffer.remaining() > 0)
            {
                Type type{};
                type.id = key;
                type.decode(buffer);
                configs[key] = std::move(type);
            }
        }
    }
};
