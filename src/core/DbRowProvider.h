#pragma once

#include "config_types/Types.h"
#include "core/CacheSource.h"
#include "core/ConfigProvider.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class DbRowProvider
{
    static constexpr int CONFIG_INDEX = 2;
    static constexpr int DB_ROW_ARCHIVE = 41;

    CacheSource *cache_;
    std::map<int, DbRowType> dbRows_;
    std::unordered_map<int64_t, std::vector<int>> tableKeyIndex_;
    bool loaded_ = false;

    static int64_t makeKey(int tableId, int col0Key)
    {
        return (static_cast<int64_t>(tableId) << 32) | static_cast<uint32_t>(col0Key);
    }

public:
    explicit DbRowProvider(CacheSource *cache) : cache_(cache) {}

    void load()
    {
        if (loaded_ || !cache_) return;

        auto &archive = cache_->archive(CONFIG_INDEX, DB_ROW_ARCHIVE);

        for (auto &[fileId, fh] : archive.files)
        {
            auto buf = archive.readFile(fileId);
            if (buf.buffer == nullptr || buf.remaining() == 0) continue;

            DbRowType row;
            row.id = fileId;
            row.decode(buf);

            if (row.tableId >= 0)
            {
                int col0Key = row.getCol0IntKey();
                if (col0Key >= 0)
                    tableKeyIndex_[makeKey(row.tableId, col0Key)].push_back(fileId);
            }

            dbRows_[fileId] = std::move(row);
        }

        loaded_ = true;
    }

    [[nodiscard]] bool isLoaded() const { return loaded_; }

    DbRowType *get(int rowId)
    {
        auto it = dbRows_.find(rowId);
        if (it == dbRows_.end()) return nullptr;
        return &it->second;
    }

    std::vector<DbRowType *> dbFind(int tableId, int col0Key)
    {
        std::vector<DbRowType *> results;
        auto it = tableKeyIndex_.find(makeKey(tableId, col0Key));
        if (it == tableKeyIndex_.end()) return results;

        for (int rowId : it->second)
        {
            auto rowIt = dbRows_.find(rowId);
            if (rowIt != dbRows_.end())
                results.push_back(&rowIt->second);
        }
        return results;
    }

    DbRowType *dbFindFirst(int tableId, int col0Key)
    {
        auto it = tableKeyIndex_.find(makeKey(tableId, col0Key));
        if (it == tableKeyIndex_.end() || it->second.empty()) return nullptr;
        auto rowIt = dbRows_.find(it->second[0]);
        if (rowIt == dbRows_.end()) return nullptr;
        return &rowIt->second;
    }
};
