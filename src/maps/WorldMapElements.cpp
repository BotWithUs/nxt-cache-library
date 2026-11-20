#include "maps/WorldMapElements.h"

#include "core/Archive.h"
#include "core/CacheSource.h"
#include "core/RSBuffer.h"

#include <algorithm>

namespace maps {

namespace {

uint32_t readU32(const uint8_t *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint16_t readU16(const uint8_t *p)
{
    return uint16_t((p[0] << 8) | p[1]);
}

}  // namespace

bool decodeWorldMapElementFile(const uint8_t *data, std::size_t size, int areaId,
                               std::vector<WorldMapElementPlacement> &outPlacements)
{
    outPlacements.clear();
    if (data == nullptr || size < 2)
    {
        return false;
    }
    const std::size_t count = readU16(data);
    if (size != 2 + count * kWorldMapElementRecordSize)
    {
        return false;
    }
    outPlacements.reserve(count);
    const uint8_t *record = data + 2;
    for (std::size_t i = 0; i < count; i++, record += kWorldMapElementRecordSize)
    {
        const uint32_t packed = readU32(record);
        outPlacements.push_back({
            areaId,
            readU16(record + 4),
            int((packed >> 14) & 0x3FFF),
            int(packed & 0x3FFF),
            int((packed >> 28) & 0x3),
            record[6],
        });
    }
    return true;
}

WorldMapAreaResult buildWorldMapAreaElements(CacheSource &source, int areaId,
                                             std::vector<WorldMapElementPlacement> &outPlacements)
{
    outPlacements.clear();
    // The reference table already says whether the area has an element file, so
    // an area without one is answered without loading its geography.
    const std::vector<int> fileIds = source.fileIds(kWorldMapElementIndex, areaId);
    const bool isDeclared = !fileIds.empty();
    const bool hasElementFile =
        std::find(fileIds.begin(), fileIds.end(), kWorldMapElementFile) != fileIds.end();
    if (isDeclared && !hasElementFile)
    {
        return WorldMapAreaResult::Ok;
    }
    Archive &archive = source.archive(kWorldMapElementIndex, areaId);
    if (archive.id == -1)
    {
        return WorldMapAreaResult::Absent;
    }
    if (archive.files.count(kWorldMapElementFile) == 0)
    {
        return WorldMapAreaResult::Ok;
    }
    RSBuffer buffer = archive.readFile(kWorldMapElementFile);
    if (buffer.buffer == nullptr)
    {
        return WorldMapAreaResult::Malformed;
    }
    const auto *bytes = reinterpret_cast<const uint8_t *>(buffer.buffer + buffer.readPosition);
    if (!decodeWorldMapElementFile(bytes, buffer.remaining(), areaId, outPlacements))
    {
        return WorldMapAreaResult::Malformed;
    }
    return WorldMapAreaResult::Ok;
}

}  // namespace maps
