#include "core/Archive.h"

#include <cstdio>
#include <iterator>

FileHeader &Archive::file(int fileId)
{
    return files[fileId];
}

void Archive::makeFile(int fileId)
{
    fileIds.insert(fileId);
    files[fileId] = FileHeader(fileId);
}

RSBuffer Archive::readFile(int fileId)
{
    auto &header = files[fileId];

    if (header.id != fileId)
    {
        return RSBuffer(nullptr, 0);
    }

    auto iterator = fileIds.find(fileId);
    if (iterator == fileIds.end())
    {
        std::fprintf(stderr, "File ID %d not found in fileIds map\n", fileId);
        return RSBuffer(nullptr, 0);
    }

    auto next = std::next(iterator);

    size_t size = 0;
    if (next != fileIds.end())
    {
        auto &nextFile = files[*next];
        size = static_cast<size_t>(nextFile.fileOffset - header.fileOffset);
    }
    else
    {
        size = data.size() - header.fileOffset;
    }

    return data.subBuffer(static_cast<size_t>(header.fileOffset), size);
}
