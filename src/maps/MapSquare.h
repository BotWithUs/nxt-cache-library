#pragma once

#include "config_types/Types.h"

#include <cstdint>
#include <unordered_map>

class CacheSource;

namespace maps {

// Directional clip flags packed per tile. The low bits mirror the RS clip model
// (one bit per wall edge plus whole-tile object/floor/blocked); the high bits are
// project-specific markers WorldWalker consumes when assembling transitions.
//
// Wall edges are stored on the tile they belong to AND reflected onto the
// adjacent tile (e.g. a west wall on T also sets WALL_E on T's west neighbour),
// matching the prior collision model. Reflections that fall outside this square's
// 64x64 grid are dropped, so a consumer MUST treat a tile edge as blocked when
// EITHER endpoint carries the corresponding wall bit (check both sides per step);
// edge-of-square walls only carry their primary side here.
enum ClipFlag : uint32_t
{
    CLIP_OPEN             = 0x0,
    CLIP_WALL_NW          = 0x1,
    CLIP_WALL_N           = 0x2,
    CLIP_WALL_NE          = 0x4,
    CLIP_WALL_E           = 0x8,
    CLIP_WALL_SE          = 0x10,
    CLIP_WALL_S           = 0x20,
    CLIP_WALL_SW          = 0x40,
    CLIP_WALL_W           = 0x80,
    CLIP_OBJECT           = 0x100,
    CLIP_FLOOR_DECORATION = 0x40000,
    CLIP_FLOOR            = 0x200000,
    CLIP_BLOCKED          = 0x1000000,
    CLIP_WATER            = 0x2000000,
    CLIP_DOOR             = 0x4000000,
    CLIP_AGILITY_SHORTCUT = 0x8000000,
    CLIP_PLANE_CHANGE     = 0x10000000,
    CLIP_CLIMBOVER        = 0x20000000,
};

// Decoded clip grid for one map square (index 5). Layout is plane-major, then x
// (west-east, 0..63), then y (south-north, 0..63), matching the cache's own tile
// addressing. planeMask has bit p set when plane p holds any non-zero tile.
struct MapSquareClip
{
    static constexpr int PLANES = 4;
    static constexpr int SIZE = 64;

    uint32_t flags[PLANES][SIZE][SIZE]{};
    uint8_t planeMask{};
};

// Memoizes the cache lookups buildMapSquareClip performs so iterating many squares
// does not re-decode location defs and overlays shared between them. Pass the same
// instance across calls; it is bound to one CacheSource for its lifetime.
struct DefCache
{
    std::unordered_map<int, LocationType> locDefs;
    std::unordered_map<int, bool> waterOverlays;
};

// Decodes terrain (file 3) and locations (file 0) for the map square at
// (squareX, squareY), resolves referenced location defs (index 16) and water
// overlays (index 2/archive 4) through defs, and writes the directional clip grid
// into outClip. Returns false (leaving outClip zeroed) when the square is absent
// from the cache; an empty-but-present square returns true with planeMask == 0.
bool buildMapSquareClip(CacheSource &source, int squareX, int squareY,
                        DefCache &defs, MapSquareClip &outClip);

}  // namespace maps
