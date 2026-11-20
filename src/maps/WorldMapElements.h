#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class CacheSource;

namespace maps {

// World map element (WME) placements, cache index 41.
//
// One index-41 archive per world map area (the archive id is the area id).
// File 0 of the archive holds the area's map geography; file 1, present only in
// areas that place elements, is the element list:
//
//   u16 count
//   count x { i32 packedCoord, u16 elementId, u8 flags }   (7 bytes each)
//
// and nothing else: the file is exactly 2 + 7 * count bytes. packedCoord is the
// usual coord grid: plane = (p >> 28) & 3, x = (p >> 14) & 0x3FFF, y = p & 0x3FFF.
// elementId is a WME definition id (config index 2, archive 36). The meaning of
// flags is not pinned; it is passed through raw.
//
// Coordinates are in the area's own map space. That is the world tile for most
// placements, but the main overview area draws a few regions (Prifddinas,
// Ashdale, ...) somewhere other than where they really are, and places their
// elements at that drawn position. The same element ids then appear again, at
// the real tile, in the region's own area archive. Consumers that need world
// tiles must therefore keep the area id.
inline constexpr int kWorldMapElementIndex = 41;
inline constexpr int kWorldMapElementFile = 1;
inline constexpr std::size_t kWorldMapElementRecordSize = 7;

struct WorldMapElementPlacement
{
    int areaId;      // index-41 archive id (world map area)
    int elementId;   // WME definition id
    int worldX;      // tile x in the area's map space
    int worldY;      // tile y in the area's map space
    int plane;       // 0..3
    int flags;       // raw trailing byte, meaning not pinned
};

// Decodes one element-list file. Returns false (outPlacements cleared) when the
// bytes do not have the exact length the count declares; nothing partial is
// returned.
bool decodeWorldMapElementFile(const uint8_t *data, std::size_t size, int areaId,
                               std::vector<WorldMapElementPlacement> &outPlacements);

enum class WorldMapAreaResult
{
    Ok,        // decoded; an area with no element file yields an empty list
    Absent,    // no such archive in index 41
    Malformed  // the element file failed decodeWorldMapElementFile
};

// Reads and decodes the element list of one world map area. outPlacements is
// cleared first and is left empty unless the result is Ok.
WorldMapAreaResult buildWorldMapAreaElements(CacheSource &source, int areaId,
                                             std::vector<WorldMapElementPlacement> &outPlacements);

}  // namespace maps
