#include "maps/MapSquare.h"

#include "core/Archive.h"
#include "core/CacheSource.h"
#include "core/RSBuffer.h"
#include "core/TypeMappings.h"
#include "config_types/VarPlayerType.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace maps {
namespace {

constexpr int kMapIndex = 5;
constexpr int kTerrainFile = 3;
constexpr int kLocationFile = 0;
constexpr int kAgilityCursor = 181;
constexpr uint8_t kTileBlocked = 0x1;
constexpr uint8_t kTileBridge = 0x2;
// VarbitType::domainType of a varbit packed into a player var (varp).
constexpr int kVarDomainPlayer = 0;

struct LocPlacement
{
    int objectId;
    int localX;
    int localY;
    int plane;
    int shape;
    int rotation;
};

struct TerrainData
{
    uint8_t renderRules[4][64][64]{};
    int16_t overlayIds[4][64][64]{};
};

// RS "smart" reader: one byte when < 128, otherwise a 16-bit value biased by
// 32768. Advances ptr; returns 0 when the stream is exhausted.
int readSmart(const unsigned char *&ptr, const unsigned char *end)
{
    if (ptr >= end)
    {
        return 0;
    }
    int peek = *ptr;
    if (peek < 128)
    {
        ++ptr;
        return peek;
    }
    if (ptr + 1 >= end)
    {
        return 0;
    }
    int value = (ptr[0] << 8) | ptr[1];
    ptr += 2;
    return value - 32768;
}

// Object-id delta reader: chains 32767 sentinels so deltas can exceed a smart.
int readSmartSizeVar(const unsigned char *&ptr, const unsigned char *end)
{
    int value = 0;
    int current = readSmart(ptr, end);
    while (current == 32767)
    {
        current = readSmart(ptr, end);
        value += 32767;
    }
    value += current;
    return value;
}

void skipLocCustomization(const unsigned char *&ptr, const unsigned char *end)
{
    if (ptr >= end)
    {
        return;
    }
    int flags = *ptr++;
    if (flags & 0x01)
    {
        ptr += 8;
    }
    if (flags & 0x02)
    {
        ptr += 2;
    }
    if (flags & 0x04)
    {
        ptr += 2;
    }
    if (flags & 0x08)
    {
        ptr += 2;
    }
    if (flags & 0x10)
    {
        ptr += 2;
    }
    else
    {
        if (flags & 0x20)
        {
            ptr += 2;
        }
        if (flags & 0x40)
        {
            ptr += 2;
        }
        if (flags & 0x80)
        {
            ptr += 2;
        }
    }
}

std::vector<LocPlacement> decodeLocationData(const char *data, size_t length)
{
    std::vector<LocPlacement> result;
    if (data == nullptr || length == 0)
    {
        return result;
    }

    const auto *ptr = reinterpret_cast<const unsigned char *>(data);
    const unsigned char *end = ptr + length;

    int objectId = -1;
    int incr;
    while ((incr = readSmartSizeVar(ptr, end)) != 0 && ptr < end)
    {
        objectId += incr;
        int location = 0;
        int step;
        while ((step = readSmart(ptr, end)) != 0 && ptr < end)
        {
            location += step - 1;
            int localX = (location >> 6) & 0x3F;
            int localY = location & 0x3F;
            int plane = location >> 12;

            if (ptr >= end)
            {
                break;
            }
            int packed = *ptr++;
            int shape = (packed >> 2) & 0x1F;
            int rotation = packed & 0x3;
            if (packed & 0x80)
            {
                skipLocCustomization(ptr, end);
            }

            if (plane >= 0 && plane < 4)
            {
                result.push_back({objectId, localX, localY, plane, shape, rotation});
            }
        }
    }
    return result;
}

// Decodes map file 3. Only the fields collision needs are kept: the per-tile
// render-rule byte (blocked/bridge bits) and the overlay id (for water).
bool decodeMapTerrain(const char *data, size_t length, TerrainData &out)
{
    if (data == nullptr || length < 5)
    {
        return false;
    }

    const auto *ptr = reinterpret_cast<const unsigned char *>(data);
    const unsigned char *end = ptr + length;
    ptr += 5;

    for (int plane = 0; plane < 4; ++plane)
    {
        for (int x = 0; x < 64; ++x)
        {
            for (int y = 0; y < 64; ++y)
            {
                if (ptr >= end)
                {
                    return true;
                }
                int streamFlags = *ptr++;
                if (streamFlags & 0x1)
                {
                    if (ptr >= end)
                    {
                        return true;
                    }
                    ++ptr;  // overlay shape+rotation byte (unused)
                    out.overlayIds[plane][x][y] = static_cast<int16_t>(readSmart(ptr, end));
                }
                if (streamFlags & 0x2)
                {
                    if (ptr >= end)
                    {
                        return true;
                    }
                    out.renderRules[plane][x][y] = *ptr++;
                }
                if (streamFlags & 0x4)
                {
                    readSmart(ptr, end);  // underlay id (unused)
                }
                if (streamFlags & 0x8)
                {
                    if (ptr + 1 >= end)
                    {
                        return true;
                    }
                    ptr += 2;  // 2-byte underlay id (unused)
                }
            }
        }
    }
    return true;
}

void setClip(MapSquareClip &clip, int plane, int lx, int ly, uint32_t flags)
{
    if (plane < 0 || plane >= MapSquareClip::PLANES)
    {
        return;
    }
    if (lx < 0 || lx >= MapSquareClip::SIZE || ly < 0 || ly >= MapSquareClip::SIZE)
    {
        return;
    }
    clip.flags[plane][lx][ly] |= flags;
}

// A straight wall (shape 0) sits on one edge of the tile and reflects the
// opposite edge onto the neighbour across it.
void addStraightWall(MapSquareClip &clip, int plane, int lx, int ly, int rotation)
{
    switch (rotation)
    {
        case 0:
            setClip(clip, plane, lx, ly, CLIP_WALL_W);
            setClip(clip, plane, lx - 1, ly, CLIP_WALL_E);
            break;
        case 1:
            setClip(clip, plane, lx, ly, CLIP_WALL_N);
            setClip(clip, plane, lx, ly + 1, CLIP_WALL_S);
            break;
        case 2:
            setClip(clip, plane, lx, ly, CLIP_WALL_E);
            setClip(clip, plane, lx + 1, ly, CLIP_WALL_W);
            break;
        case 3:
            setClip(clip, plane, lx, ly, CLIP_WALL_S);
            setClip(clip, plane, lx, ly - 1, CLIP_WALL_N);
            break;
        default:
            break;
    }
}

// A diagonal wall (shapes 1 and 3) blocks a corner and reflects onto the
// diagonally opposite neighbour.
void addDiagonalWall(MapSquareClip &clip, int plane, int lx, int ly, int rotation)
{
    switch (rotation)
    {
        case 0:
            setClip(clip, plane, lx, ly, CLIP_WALL_NW);
            setClip(clip, plane, lx - 1, ly + 1, CLIP_WALL_SE);
            break;
        case 1:
            setClip(clip, plane, lx, ly, CLIP_WALL_NE);
            setClip(clip, plane, lx + 1, ly + 1, CLIP_WALL_SW);
            break;
        case 2:
            setClip(clip, plane, lx, ly, CLIP_WALL_SE);
            setClip(clip, plane, lx + 1, ly - 1, CLIP_WALL_NW);
            break;
        case 3:
            setClip(clip, plane, lx, ly, CLIP_WALL_SW);
            setClip(clip, plane, lx - 1, ly - 1, CLIP_WALL_NE);
            break;
        default:
            break;
    }
}

// A corner wall (shape 2) blocks two adjacent edges and reflects each onto its
// respective neighbour.
void addCornerWall(MapSquareClip &clip, int plane, int lx, int ly, int rotation)
{
    switch (rotation)
    {
        case 0:
            setClip(clip, plane, lx, ly, CLIP_WALL_W | CLIP_WALL_N);
            setClip(clip, plane, lx - 1, ly, CLIP_WALL_E);
            setClip(clip, plane, lx, ly + 1, CLIP_WALL_S);
            break;
        case 1:
            setClip(clip, plane, lx, ly, CLIP_WALL_N | CLIP_WALL_E);
            setClip(clip, plane, lx, ly + 1, CLIP_WALL_S);
            setClip(clip, plane, lx + 1, ly, CLIP_WALL_W);
            break;
        case 2:
            setClip(clip, plane, lx, ly, CLIP_WALL_E | CLIP_WALL_S);
            setClip(clip, plane, lx + 1, ly, CLIP_WALL_W);
            setClip(clip, plane, lx, ly - 1, CLIP_WALL_N);
            break;
        case 3:
            setClip(clip, plane, lx, ly, CLIP_WALL_S | CLIP_WALL_W);
            setClip(clip, plane, lx, ly - 1, CLIP_WALL_N);
            setClip(clip, plane, lx - 1, ly, CLIP_WALL_E);
            break;
        default:
            break;
    }
}

void addWallCollision(MapSquareClip &clip, int plane, int lx, int ly, int shape, int rotation)
{
    if (shape == 0)
    {
        addStraightWall(clip, plane, lx, ly, rotation);
    }
    else if (shape == 1 || shape == 3)
    {
        addDiagonalWall(clip, plane, lx, ly, rotation);
    }
    else if (shape == 2)
    {
        addCornerWall(clip, plane, lx, ly, rotation);
    }
}

void addObjectCollision(MapSquareClip &clip, int plane, int lx, int ly, int sizeX, int sizeY)
{
    for (int dx = 0; dx < sizeX; ++dx)
    {
        for (int dy = 0; dy < sizeY; ++dy)
        {
            setClip(clip, plane, lx + dx, ly + dy, CLIP_OBJECT);
        }
    }
}

bool containsCaseInsensitive(const std::string &haystack, const char *needle)
{
    std::string lower = haystack;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower.find(needle) != std::string::npos;
}

bool hasAnyOption(const LocationType &loc)
{
    for (const std::string &opt : loc.options)
    {
        if (!opt.empty())
        {
            return true;
        }
    }
    return false;
}

bool isClimbOver(const std::string &opt)
{
    return containsCaseInsensitive(opt, "over")
        || containsCaseInsensitive(opt, "across")
        || containsCaseInsensitive(opt, "through");
}

bool hasClimbOverOption(const LocationType &loc)
{
    for (const std::string &opt : loc.options)
    {
        if (opt.empty())
        {
            continue;
        }
        if (containsCaseInsensitive(opt, "climb") && isClimbOver(opt))
        {
            return true;
        }
        if (containsCaseInsensitive(opt, "cross"))
        {
            return true;
        }
        if (containsCaseInsensitive(opt, "squeeze"))
        {
            return true;
        }
        if (containsCaseInsensitive(opt, "jump"))
        {
            return true;
        }
    }
    return false;
}

// True when an option implies a vertical plane change (ladder/stair), excluding
// climb-over shortcuts. Mirrors classifyClimbOptions() != CLIMB_NONE.
bool hasPlaneChangeOption(const LocationType &loc)
{
    for (const std::string &opt : loc.options)
    {
        if (opt.empty())
        {
            continue;
        }
        bool hasClimb = containsCaseInsensitive(opt, "climb");
        if (hasClimb && isClimbOver(opt))
        {
            continue;
        }
        if (hasClimb || containsCaseInsensitive(opt, "ascend")
            || containsCaseInsensitive(opt, "descend"))
        {
            return true;
        }
    }
    return false;
}

bool hasAgilityCursor(const LocationType &loc)
{
    for (int cursor : loc.cursors)
    {
        if (cursor == kAgilityCursor)
        {
            return true;
        }
    }
    return false;
}

// 0xFF when no interactive option slot applies — the crossing carries no clickable
// option. Otherwise the 0-based option slot.
constexpr uint8_t kNoOption = 0xFF;

uint8_t firstOptionIndex(const LocationType &loc)
{
    for (std::size_t i = 0; i < loc.options.size(); ++i)
    {
        if (!loc.options[i].empty())
        {
            return static_cast<uint8_t>(i);
        }
    }
    return kNoOption;
}

uint8_t climbOverOptionIndex(const LocationType &loc)
{
    for (std::size_t i = 0; i < loc.options.size(); ++i)
    {
        const std::string &opt = loc.options[i];
        if (opt.empty())
        {
            continue;
        }
        if ((containsCaseInsensitive(opt, "climb") && isClimbOver(opt))
            || containsCaseInsensitive(opt, "cross")
            || containsCaseInsensitive(opt, "squeeze")
            || containsCaseInsensitive(opt, "jump"))
        {
            return static_cast<uint8_t>(i);
        }
    }
    return firstOptionIndex(loc);
}

uint8_t planeChangeOptionIndex(const LocationType &loc)
{
    for (std::size_t i = 0; i < loc.options.size(); ++i)
    {
        const std::string &opt = loc.options[i];
        if (opt.empty())
        {
            continue;
        }
        const bool climb = containsCaseInsensitive(opt, "climb");
        if (climb && isClimbOver(opt))
        {
            continue;
        }
        if (climb || containsCaseInsensitive(opt, "ascend")
            || containsCaseInsensitive(opt, "descend"))
        {
            return static_cast<uint8_t>(i);
        }
    }
    return firstOptionIndex(loc);
}

uint8_t agilityOptionIndex(const LocationType &loc)
{
    constexpr std::size_t cursorCount = sizeof(loc.cursors) / sizeof(loc.cursors[0]);
    for (std::size_t i = 0; i < cursorCount; ++i)
    {
        if (loc.cursors[i] == kAgilityCursor)
        {
            return static_cast<uint8_t>(i);
        }
    }
    return firstOptionIndex(loc);
}

// bit0 (kClimbUp) / bit1 (kClimbDown) per the loc's climb options. Mirrors the
// prior nav stack's classifyClimbOptions: "up"/"ascend" => up, "down"/"descend"
// => down, an ambiguous "climb" => both. Climb-over options are excluded.
uint8_t classifyClimbDir(const LocationType &loc)
{
    uint8_t dir = 0;
    for (const std::string &opt : loc.options)
    {
        if (opt.empty())
        {
            continue;
        }
        const bool climb = containsCaseInsensitive(opt, "climb");
        if (climb && isClimbOver(opt))
        {
            continue;
        }
        const bool ascend = containsCaseInsensitive(opt, "ascend");
        const bool descend = containsCaseInsensitive(opt, "descend");
        if (!climb && !ascend && !descend)
        {
            continue;
        }
        if (containsCaseInsensitive(opt, "up") || ascend)
        {
            dir |= kClimbUp;
        }
        else if (containsCaseInsensitive(opt, "down") || descend)
        {
            dir |= kClimbDown;
        }
        else
        {
            dir |= static_cast<uint8_t>(kClimbUp | kClimbDown);
        }
    }
    return dir;
}

// Append a Crossing for this placement when a sink is provided. worldBaseX/Y are
// the square's south-west world-tile origin (squareX * 64, squareY * 64).
void emitCrossing(std::vector<Crossing> *outCrossings, CrossingKind kind,
                  const LocationType &locType, const LocPlacement &loc, int effectivePlane,
                  int worldBaseX, int worldBaseY, uint8_t optionIndex, uint8_t climbDir)
{
    if (outCrossings == nullptr)
    {
        return;
    }
    int sizeX = locType.sizeX > 0 ? locType.sizeX : 1;
    int sizeY = locType.sizeY > 0 ? locType.sizeY : 1;
    if (loc.rotation == 1 || loc.rotation == 3)
    {
        std::swap(sizeX, sizeY);
    }
    Crossing c{};
    c.objectId    = loc.objectId;
    c.worldX      = static_cast<uint16_t>(worldBaseX + loc.localX);
    c.worldY      = static_cast<uint16_t>(worldBaseY + loc.localY);
    c.plane       = static_cast<uint8_t>(effectivePlane);
    c.shape       = static_cast<uint8_t>(loc.shape);
    c.rotation    = static_cast<uint8_t>(loc.rotation);
    c.kind        = static_cast<uint8_t>(kind);
    c.sizeX       = static_cast<uint8_t>(sizeX);
    c.sizeY       = static_cast<uint8_t>(sizeY);
    c.optionIndex = optionIndex;
    c.climbDir    = climbDir;
    outCrossings->push_back(c);
}

const LocationType *locDef(CacheSource &source, DefCache &defs, int objectId)
{
    auto it = defs.locDefs.find(objectId);
    if (it != defs.locDefs.end())
    {
        return &it->second;
    }

    const nxt::TypeMapping &mapping = nxt::typeDefaults().at("loc");
    int archiveId = objectId >> mapping.shift;
    int fileId = objectId & ((1 << mapping.shift) - 1);
    Archive &archive = source.archive(mapping.indexId, archiveId);
    if (archive.id == -1)
    {
        return nullptr;
    }
    RSBuffer buffer = archive.readFile(fileId);
    if (buffer.buffer == nullptr || buffer.remaining() == 0)
    {
        return nullptr;
    }

    LocationType locType{};
    locType.id = objectId;
    locType.decode(buffer);
    auto inserted = defs.locDefs.emplace(objectId, std::move(locType));
    return &inserted.first->second;
}

// The value an unset varp reads as: the client's default rule, narrowed to the
// int a transform index is. A varp with no type, a string type, or no def reads 0.
int defaultVarpValue(CacheSource &source, DefCache &defs, int varpId)
{
    auto it = defs.varpValues.find(varpId);
    if (it != defs.varpValues.end())
    {
        return it->second;
    }

    int value = 0;
    const nxt::TypeMapping &mapping = nxt::typeDefaults().at("varp");
    Archive &archive = source.archive(mapping.indexId, mapping.archiveId);
    // Membership first: Archive::readFile inserts a default header for an unknown id.
    if (archive.id != -1 && archive.fileIds.count(varpId) != 0)
    {
        RSBuffer buffer = archive.readFile(varpId);
        const auto *bytes = reinterpret_cast<const uint8_t *>(buffer.buffer);
        VarPlayerType type{};
        type.id = varpId;
        if (bytes != nullptr && type.decodeStrict(bytes + buffer.readPosition, buffer.remaining()))
        {
            const VarpDefault def = type.resolveDefault();
            if (def.rule != VarpDefaultRule::None && def.text == nullptr)
            {
                value = static_cast<int>(def.value);
            }
        }
    }
    defs.varpValues.emplace(varpId, value);
    return value;
}

// A varbit's value for a fresh player: its bit range of the owning varp's default.
int defaultVarbitValue(CacheSource &source, DefCache &defs, int varbitId)
{
    auto it = defs.varbitValues.find(varbitId);
    if (it != defs.varbitValues.end())
    {
        return it->second;
    }

    int value = 0;
    const nxt::TypeMapping &mapping = nxt::typeDefaults().at("varbit");
    Archive &archive = source.archive(mapping.indexId, mapping.archiveId);
    if (archive.id != -1 && archive.fileIds.count(varbitId) != 0)
    {
        RSBuffer buffer = archive.readFile(varbitId);
        VarbitType varbit{};
        varbit.id = varbitId;
        if (buffer.buffer != nullptr && buffer.remaining() > 0)
        {
            varbit.decode(buffer);
        }
        const bool isRangeValid = varbit.lsb >= 0 && varbit.msb >= varbit.lsb && varbit.msb <= 31;
        if (varbit.domainType == kVarDomainPlayer && isRangeValid)
        {
            const auto raw = static_cast<uint32_t>(defaultVarpValue(source, defs, varbit.varId));
            const uint32_t width = static_cast<uint32_t>(varbit.msb - varbit.lsb) + 1u;
            const uint32_t mask = width >= 32u ? 0xFFFFFFFFu : ((1u << width) - 1u);
            value = static_cast<int>((raw >> varbit.lsb) & mask);
        }
    }
    defs.varbitValues.emplace(varbitId, value);
    return value;
}

bool isWaterOverlay(CacheSource &source, DefCache &defs, int overlayId)
{
    auto it = defs.waterOverlays.find(overlayId);
    if (it != defs.waterOverlays.end())
    {
        return it->second;
    }

    const nxt::TypeMapping &mapping = nxt::typeDefaults().at("overlay");
    bool isWater = false;
    Archive &archive = source.archive(mapping.indexId, mapping.archiveId);
    if (archive.id != -1)
    {
        RSBuffer buffer = archive.readFile(overlayId);
        if (buffer.buffer != nullptr && buffer.remaining() > 0)
        {
            OverlayType overlay{};
            overlay.id = overlayId;
            overlay.decode(buffer);
            isWater = overlay.isWater;
        }
    }
    defs.waterOverlays.emplace(overlayId, isWater);
    return isWater;
}

void applyTerrain(MapSquareClip &clip, const TerrainData &terrain)
{
    for (int plane = 0; plane < 4; ++plane)
    {
        for (int x = 0; x < 64; ++x)
        {
            for (int y = 0; y < 64; ++y)
            {
                if ((terrain.renderRules[plane][x][y] & kTileBlocked) == 0)
                {
                    continue;
                }
                int effectivePlane = plane;
                if (terrain.renderRules[1][x][y] & kTileBridge)
                {
                    effectivePlane = plane - 1;
                }
                if (effectivePlane >= 0)
                {
                    setClip(clip, effectivePlane, x, y, CLIP_FLOOR | CLIP_BLOCKED);
                }
            }
        }
    }
}

// Option-driven classification for one placement: agility / climb-over /
// plane-change markers and their crossings. `placed` supplies the footprint,
// `active` (the variant the player sees) the options and cursors.
void applyInteractions(MapSquareClip &clip, const LocationType &placed,
                       const LocationType &active, const LocPlacement &loc, int effectivePlane,
                       int worldBaseX, int worldBaseY, std::vector<Crossing> *outCrossings)
{
    if (hasAgilityCursor(active))
    {
        setClip(clip, effectivePlane, loc.localX, loc.localY, CLIP_AGILITY_SHORTCUT);
        emitCrossing(outCrossings, CrossingKind::Agility, placed, loc, effectivePlane,
                     worldBaseX, worldBaseY, agilityOptionIndex(active), 0);
    }
    else if (hasClimbOverOption(active))
    {
        setClip(clip, effectivePlane, loc.localX, loc.localY, CLIP_CLIMBOVER);
        emitCrossing(outCrossings, CrossingKind::ClimbOver, placed, loc, effectivePlane,
                     worldBaseX, worldBaseY, climbOverOptionIndex(active), 0);
    }
    if (hasPlaneChangeOption(active))
    {
        setClip(clip, effectivePlane, loc.localX, loc.localY, CLIP_PLANE_CHANGE);
        emitCrossing(outCrossings, CrossingKind::PlaneChange, placed, loc, effectivePlane,
                     worldBaseX, worldBaseY, planeChangeOptionIndex(active),
                     classifyClimbDir(active));
    }
}

// `placed` is the def the map names; `active` is resolveActiveLoc's variant, or
// nullptr when the multiloc is absent at the current var value. Collision always
// comes from `placed`; options (and so crossings) only from `active`.
void applyPlacement(MapSquareClip &clip, const LocationType &placed,
                    const LocationType *active, const LocPlacement &loc, int effectivePlane,
                    int worldBaseX, int worldBaseY, std::vector<Crossing> *outCrossings)
{
    if (active != nullptr)
    {
        applyInteractions(clip, placed, *active, loc, effectivePlane,
                          worldBaseX, worldBaseY, outCrossings);
    }

    if (placed.solidType == 0)
    {
        return;
    }

    const bool isDoor = placed.interactType > 0 && active != nullptr && hasAnyOption(*active);
    const uint8_t doorOption = active != nullptr ? firstOptionIndex(*active) : kNoOption;
    if (loc.shape >= 0 && loc.shape <= 3)
    {
        addWallCollision(clip, effectivePlane, loc.localX, loc.localY, loc.shape, loc.rotation);
        if (isDoor)
        {
            setClip(clip, effectivePlane, loc.localX, loc.localY, CLIP_DOOR);
            emitCrossing(outCrossings, CrossingKind::Door, placed, loc, effectivePlane,
                         worldBaseX, worldBaseY, doorOption, 0);
        }
    }
    else if (loc.shape >= 9 && loc.shape <= 21)
    {
        int sizeX = placed.sizeX > 0 ? placed.sizeX : 1;
        int sizeY = placed.sizeY > 0 ? placed.sizeY : 1;
        if (loc.rotation == 1 || loc.rotation == 3)
        {
            std::swap(sizeX, sizeY);
        }
        addObjectCollision(clip, effectivePlane, loc.localX, loc.localY, sizeX, sizeY);
        if (loc.shape == 9 && isDoor)
        {
            setClip(clip, effectivePlane, loc.localX, loc.localY, CLIP_DOOR);
            emitCrossing(outCrossings, CrossingKind::Door, placed, loc, effectivePlane,
                         worldBaseX, worldBaseY, doorOption, 0);
        }
    }
    else if (loc.shape == 22 && placed.solidType == 1)
    {
        setClip(clip, effectivePlane, loc.localX, loc.localY, CLIP_FLOOR_DECORATION);
    }
}

void applyLocations(CacheSource &source, DefCache &defs, MapSquareClip &clip,
                    const TerrainData &terrain, bool hasTerrain,
                    const std::vector<LocPlacement> &placements,
                    int worldBaseX, int worldBaseY, std::vector<Crossing> *outCrossings)
{
    for (const LocPlacement &loc : placements)
    {
        int effectivePlane = loc.plane;
        if (hasTerrain && (terrain.renderRules[1][loc.localX][loc.localY] & kTileBridge))
        {
            effectivePlane = loc.plane - 1;
            if (effectivePlane < 0)
            {
                continue;
            }
        }
        const LocationType *locType = locDef(source, defs, loc.objectId);
        if (locType == nullptr)
        {
            continue;
        }
        const LocationType *active = resolveActiveLoc(source, defs, *locType);
        applyPlacement(clip, *locType, active, loc, effectivePlane,
                       worldBaseX, worldBaseY, outCrossings);
    }
}

void applyWater(CacheSource &source, DefCache &defs, MapSquareClip &clip,
                const TerrainData &terrain)
{
    for (int plane = 0; plane < 4; ++plane)
    {
        for (int x = 0; x < 64; ++x)
        {
            for (int y = 0; y < 64; ++y)
            {
                int overlayId = terrain.overlayIds[plane][x][y];
                if (overlayId > 0 && isWaterOverlay(source, defs, overlayId))
                {
                    setClip(clip, plane, x, y, CLIP_WATER);
                }
            }
        }
    }
}

void computePlaneMask(MapSquareClip &clip)
{
    for (int plane = 0; plane < MapSquareClip::PLANES; ++plane)
    {
        for (int x = 0; x < MapSquareClip::SIZE; ++x)
        {
            for (int y = 0; y < MapSquareClip::SIZE; ++y)
            {
                if (clip.flags[plane][x][y] != 0)
                {
                    clip.planeMask |= static_cast<uint8_t>(1 << plane);
                }
            }
        }
    }
}

}  // namespace

const LocationType *resolveActiveLoc(CacheSource &source, DefCache &defs,
                                     const LocationType &base)
{
    if (base.transforms.empty())
    {
        return &base;
    }

    int index = -1;
    if (base.varbitId != -1)
    {
        index = defaultVarbitValue(source, defs, base.varbitId);
    }
    else if (base.varpId != -1)
    {
        index = defaultVarpValue(source, defs, base.varpId);
    }

    const int count = static_cast<int>(base.transforms.size());
    const bool isInside = index >= 0 && index < count;
    const int activeId = base.transforms[static_cast<std::size_t>(isInside ? index : count - 1)];
    if (activeId < 0)
    {
        return nullptr;
    }
    return locDef(source, defs, activeId);
}

bool buildMapSquareClip(CacheSource &source, int squareX, int squareY,
                        DefCache &defs, MapSquareClip &outClip,
                        std::vector<Crossing> *outCrossings)
{
    outClip = MapSquareClip{};
    if (outCrossings != nullptr)
    {
        outCrossings->clear();
    }

    int archiveId = (squareX & 0x7F) | (squareY << 7);
    Archive &archive = source.archive(kMapIndex, archiveId);
    if (archive.id == -1)
    {
        return false;
    }

    const int worldBaseX = squareX * MapSquareClip::SIZE;
    const int worldBaseY = squareY * MapSquareClip::SIZE;

    TerrainData terrain{};
    bool hasTerrain = false;
    if (archive.files.count(kTerrainFile) != 0)
    {
        RSBuffer buffer = archive.readFile(kTerrainFile);
        if (buffer.buffer != nullptr && buffer.remaining() > 0)
        {
            hasTerrain = decodeMapTerrain(buffer.buffer + buffer.readPosition,
                                          buffer.remaining(), terrain);
        }
    }
    if (hasTerrain)
    {
        applyTerrain(outClip, terrain);
    }

    if (archive.files.count(kLocationFile) != 0)
    {
        RSBuffer buffer = archive.readFile(kLocationFile);
        if (buffer.buffer != nullptr && buffer.remaining() > 0)
        {
            std::vector<LocPlacement> placements =
                decodeLocationData(buffer.buffer + buffer.readPosition, buffer.remaining());
            applyLocations(source, defs, outClip, terrain, hasTerrain, placements,
                           worldBaseX, worldBaseY, outCrossings);
        }
    }

    if (hasTerrain)
    {
        applyWater(source, defs, outClip, terrain);
    }

    computePlaneMask(outClip);
    return true;
}

std::vector<LocSpawn> buildMapSquareLocs(CacheSource &source, int squareX, int squareY)
{
    std::vector<LocSpawn> result;
    buildMapSquareLocs(source, squareX, squareY, result);
    return result;
}

bool buildMapSquareLocs(CacheSource &source, int squareX, int squareY,
                        std::vector<LocSpawn> &outLocs)
{
    outLocs.clear();

    int archiveId = (squareX & 0x7F) | (squareY << 7);
    Archive &archive = source.archive(kMapIndex, archiveId);
    if (archive.id == -1)
    {
        return false;
    }
    if (archive.files.count(kLocationFile) == 0)
    {
        return true;
    }

    RSBuffer buffer = archive.readFile(kLocationFile);
    if (buffer.buffer == nullptr || buffer.remaining() == 0)
    {
        return true;
    }

    const int worldBaseX = squareX * MapSquareClip::SIZE;
    const int worldBaseY = squareY * MapSquareClip::SIZE;

    std::vector<LocPlacement> placements =
        decodeLocationData(buffer.buffer + buffer.readPosition, buffer.remaining());
    outLocs.reserve(placements.size());
    for (const LocPlacement &loc : placements)
    {
        outLocs.push_back({loc.objectId, worldBaseX + loc.localX, worldBaseY + loc.localY,
                           loc.plane, loc.shape, loc.rotation});
    }
    return true;
}

}  // namespace maps
