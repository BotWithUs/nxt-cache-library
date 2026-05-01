#include "core/Index.h"

#include "gzip/decompress.hpp"
#include "network/Js5Archive.h"
#include "network/Js5Compression.h"

#include <sqlite3.h>

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

Index::Index(int id, const std::string &file, FallbackFn fallback)
    : id(id), fallback_(std::move(fallback))
{
    auto rc = sqlite3_open(file.c_str(), &db);
    if (rc != SQLITE_OK)
    {
        std::printf("Failed to open %s database: %s\n", file.c_str(), sqlite3_errmsg(db));
    }
    RSBuffer buffer(0);
    readReferenceBlob(buffer);

    if (buffer.size() == 0 && fallback_)
    {
        // sqlite has no reference table for this index — try the live source.
        try
        {
            auto raw = fallback_(255, id);
            if (raw && !raw->empty())
            {
                auto decompressed = js5::decompress(raw->data(), raw->size());
                buffer.writeFully(reinterpret_cast<char *>(decompressed.data()),
                                   decompressed.size());
                decodeReferenceBlob(buffer);
                return;
            }
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "Live fallback for ref table %d failed: %s\n",
                         id, e.what());
        }
    }

    Index::decompress(buffer);
    decodeReferenceBlob(buffer);
}

Index::~Index()
{
    sqlite3_close(db);
}

Archive &Index::archive(int archiveId)
{
    auto &archive = archives[archiveId];
    if (archive.loaded || archive.id == -1)
    {
        return archive;
    }
    RSBuffer buffer(0);
    char err = readArchiveBlob(archiveId, buffer);
    if (err != 0)
    {
        std::printf("Failed to read archive blob\n");
    }

    if (buffer.size() == 0 && fallback_)
    {
        // sqlite has no blob for this archive — fall back to the live source.
        try
        {
            auto raw = fallback_(id, archiveId);
            if (raw && !raw->empty())
            {
                auto decompressed = js5::decompress(raw->data(), raw->size());
                std::vector<int> subIds(archive.fileIds.begin(), archive.fileIds.end());
                js5::unpackNetworkArchive(decompressed, subIds, archive);
                archive.loaded = true;
                return archive;
            }
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr,
                         "Live fallback for archive %d.%d failed: %s\n",
                         id, archiveId, e.what());
        }
    }

    err = decodeArchiveBlob(archive, buffer);
    if (err != 0)
    {
        std::printf("Failed to decode archive blob\n");
    }
    archive.loaded = true;
    return archive;
}

char Index::readReferenceBlob(RSBuffer &buffer)
{
    if (id < 0 || id > 255)
    {
        std::printf("Invalid index id.\n");
        return INDEX_ID_OUT_OF_BOUNDS;
    }

    if (!db)
    {
        std::printf("Database connection is null or not open.\n");
        return SQL_DATABASE_NOT_OPEN;
    }

    if (sqlite3_errcode(db) == SQLITE_MISUSE)
    {
        std::printf("Database connection is closed or invalid.\n");
        return SQL_DATABASE_IS_CLOSED;
    }

    if (sqlite3_db_readonly(db, nullptr) == -1)
    {
        std::printf("Database handle is invalid or not writable.\n");
        return SQL_DATABASE_NOT_READABLE;
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT key, data FROM cache_index";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        std::printf("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return SQL_STATEMENT_FAILURE;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        sqlite3_column_int(stmt, 0);
        const void *blob_data = sqlite3_column_blob(stmt, 1);
        int blob_size = sqlite3_column_bytes(stmt, 1);
        buffer.writeFully((char *) blob_data, blob_size);
    }

    sqlite3_finalize(stmt);
    return INDEX_OK;
}

char Index::readArchiveBlob(int archiveId, RSBuffer &buffer)
{
    if (!db)
    {
        std::printf("Database connection is null or not open.\n");
        return SQL_DATABASE_NOT_OPEN;
    }

    if (sqlite3_errcode(db) == SQLITE_MISUSE)
    {
        std::printf("Database connection is closed or invalid.\n");
        return SQL_DATABASE_IS_CLOSED;
    }

    if (sqlite3_db_readonly(db, nullptr) == -1)
    {
        std::printf("Database handle is invalid or not writable.\n");
        return SQL_DATABASE_NOT_READABLE;
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT key, data FROM cache WHERE key = ?";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        std::printf("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return SQL_STATEMENT_FAILURE;
    }

    rc = sqlite3_bind_int(stmt, 1, archiveId);
    if (rc != SQLITE_OK)
    {
        std::printf("Failed to bind parameter: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return SQL_BIND_FAILURE;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        int key = sqlite3_column_int(stmt, 0);
        const void *blob_data = sqlite3_column_blob(stmt, 1);
        int blob_size = sqlite3_column_bytes(stmt, 1);

        if (blob_data && blob_size > 0)
        {
            buffer.writeFully((char *) blob_data, blob_size);
        }
        else
        {
            std::printf("No blob data found for key %d.\n", key);
        }
    }

    if (rc != SQLITE_DONE)
    {
        std::printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return SQL_EXECUTION_FAILURE;
    }

    sqlite3_finalize(stmt);
    return 0;
}

char Index::decodeReferenceBlob(RSBuffer &buffer)
{
    unsigned char format = buffer.readUnsignedByte();
    if (format < 5 || format > 7)
    {
        std::printf("Invalid Format %d\n", format);
        return 1;
    }
    version = format >= 6 ? buffer.readInt() : 0;
    mask = buffer.readUnsignedByte();

    bool hasNames = (mask & 0x1) != 0;
    bool hasWhirlpools = (mask & 0x2) != 0;
    bool hasSizes = (mask & 0x4) != 0;
    bool hasHashes = (mask & 0x8) != 0;

    auto readFormat = [&]() -> int {
        return format >= 7 ? buffer.readSmartInt() : buffer.readUnsignedShort();
    };

    int archiveIdsCount = readFormat();
    archiveIds = std::vector<int>(archiveIdsCount);
    for (int i = 0; i < archiveIdsCount; i++)
    {
        int archiveId = readFormat() + (i == 0 ? 0 : archiveIds[i - 1]);
        archiveIds[i] = archiveId;
        archives[archiveId] = Archive(archiveId);
    }
    if (hasNames)
    {
        for (auto &&archiveId: archiveIds)
        {
            archives[archiveId].nameHash = buffer.readInt();
        }
    }
    for (auto &&archiveId: archiveIds)
    {
        archives[archiveId].crc = buffer.readInt();
    }
    if (hasHashes)
    {
        for (auto &&archiveId: archiveIds)
        {
            archives[archiveId].hash = buffer.readInt();
        }
    }
    if (hasWhirlpools)
    {
        for (auto &&archiveId: archiveIds)
        {
            buffer.read(archives[archiveId].whirlpool, 64);
        }
    }
    if (hasSizes)
    {
        for (auto &&archiveId: archiveIds)
        {
            archives[archiveId].compressedSize = buffer.readInt();
            archives[archiveId].uncompressedSize = buffer.readInt();
        }
    }
    for (auto &&archiveId: archiveIds)
    {
        archives[archiveId].version = buffer.readInt();
    }

    std::vector<std::vector<int>> archiveFileIds(archives.size());
    for (auto &v: archiveFileIds)
    {
        v.resize(readFormat());
    }

    for (size_t i = 0; i < archiveIds.size(); i++)
    {
        auto &archive = archives[archiveIds[i]];
        auto &fileIds = archiveFileIds[i];
        int fileId = 0;
        for (int &j: fileIds)
        {
            fileId += readFormat();
            archive.makeFile(fileId);
            j = fileId;
        }
    }

    if (hasNames)
    {
        for (size_t i = 0; i < archiveIds.size(); i++)
        {
            auto &archive = archives[archiveIds[i]];
            for (auto &fileId: archiveFileIds[i])
            {
                archive.file(fileId).name = buffer.readInt();
            }
        }
    }
    return 0;
}

char Index::decompress(RSBuffer &buffer)
{
    if (buffer.remaining() >= 4)
    {
        unsigned char z = buffer.readUnsignedByte();
        unsigned char l = buffer.readUnsignedByte();
        unsigned char b = buffer.readUnsignedByte();
        unsigned char level = buffer.readUnsignedByte();
        if (z == 'Z' && l == 'L' && b == 'B' && level == 0x1)
        {
            auto uncompressed_size = buffer.readInt();
            auto compressed_size = buffer.remaining();
            auto compressed_data = new char[compressed_size];
            std::memset(compressed_data, 0, compressed_size);
            buffer.read(compressed_data, compressed_size);
            auto uncompressed = gzip::decompress(compressed_data, compressed_size);
            delete[] compressed_data;
            if (static_cast<size_t>(uncompressed_size) != uncompressed.size())
            {
                std::printf("Uncompressed size mismatch\n");
                return FAILED_DECOMPRESSION;
            }
            buffer.writeFully(uncompressed.data(), uncompressed.size());
            return DECOMPRESSION_OK;
        }
        else
        {
            unsigned char type = buffer.readUnsignedByte();
            std::printf("Decompression type: %d\n", type);
            if (type == 0)
            {
                return DECOMPRESSION_OK;
            }
            else if (type == 1)
            {
                return DECOMPRESSION_OK;
            }
            else if (type == 2)
            {
                // TODO: gzip
                return DECOMPRESSION_OK;
            }
            else if (type == 3)
            {
                // TODO: LZMA
                return DECOMPRESSION_OK;
            }
        }
    }
    return FAILED_DECOMPRESSION;
}

char Index::decodeArchiveBlob(Archive &archive, RSBuffer &buffer)
{
    auto err = Index::decompress(buffer);
    if (err != DECOMPRESSION_OK)
    {
        return err;
    }
    if (archive.fileIds.size() == 1)
    {
        for (const auto &item: archive.fileIds)
        {
            archive.files[item].fileOffset = 0;
        }
    }
    else
    {
        unsigned char first = buffer.readUnsignedByte();
        if (first != 1)
        {
            return ARCHIVE_BAD_BUFFER;
        }
        for (const auto &item: archive.fileIds)
        {
            archive.files[item].fileOffset = buffer.readInt();
        }
    }
    archive.data.writeFully(buffer.buffer, buffer.capacity);
    return 0;
}
