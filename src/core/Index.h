#pragma once

#include "core/Archive.h"
#include "core/RSBuffer.h"

#include <map>
#include <string>
#include <vector>

struct sqlite3;

#define INDEX_OK 0
#define SQL_STATEMENT_FAILURE 1
#define SQL_DATABASE_NOT_OPEN 2
#define INDEX_ID_OUT_OF_BOUNDS 3
#define SQL_DATABASE_NOT_READABLE 4
#define SQL_DATABASE_IS_CLOSED 5
#define DECOMPRESSION_OK 6
#define SQL_BIND_FAILURE 7
#define SQL_EXECUTION_FAILURE 8
#define ARCHIVE_BAD_BUFFER 9
#define FAILED_DECOMPRESSION 10

class Index
{
    sqlite3 *db{};
    int mask{};
public:
    int id;
    int version{};
    std::vector<int> archiveIds;
    std::map<int, Archive> archives;

    explicit Index(int id, const std::string &file);

    ~Index();

    Index(const Index &) = delete;
    Index &operator=(const Index &) = delete;

    Archive &archive(int archiveId);

private:
    char readReferenceBlob(RSBuffer &buffer);

    char decodeReferenceBlob(RSBuffer &buffer);

    char readArchiveBlob(int archiveId, RSBuffer &buffer);

    static char decodeArchiveBlob(Archive &archive, RSBuffer &buffer);

    static char decompress(RSBuffer &buffer);
};
