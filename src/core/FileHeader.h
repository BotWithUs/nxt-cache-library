#pragma once

#include "core/RSBuffer.h"

class FileHeader
{
public:
    int id;
    int version{};
    int name{};
    int fileOffset{};

    explicit FileHeader(int id) : id(id) {}

    explicit FileHeader() : id(-1) {}
};
