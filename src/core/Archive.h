#pragma once

#include "core/FileHeader.h"
#include "core/RSBuffer.h"

#include <map>
#include <set>

class Archive
{
public:
    int id;
    int nameHash{};
    int crc{};
    int hash{};
    int version{};
    int compressedSize{};
    int uncompressedSize{};
    bool loaded = false;
    char whirlpool[64]{};
    std::multiset<int> fileIds;
    std::map<int, FileHeader> files;
    RSBuffer data;

    explicit Archive(int id) : id(id), data(nullptr, 0) {}

    explicit Archive() : id(-1), data(nullptr, 0) {}

    FileHeader &file(int fileId);

    void makeFile(int fileId);

    RSBuffer readFile(int fileId);
};
