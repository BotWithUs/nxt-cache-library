// Door crossings for multilocs, against a real cache. A multiloc's placed def
// usually has no options of its own: they live on the transform its var selects.
// nxt_get_mapsquare_clip_and_crossings must classify such a placement, and pick
// its option slot, from that active variant (offline: the variant a fresh player
// sees), so the crossing carries something a walker can click.
//
//   nxtcache-multiloc-doors-test [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP).
//
// The expectations are pinned constants read off the cache by hand, not
// recomputed here: each pin first checks the cache still says what it said when
// the pin was taken, so a cache update shows up as a failed precondition and not
// as a silent change of meaning.

#include "c_api/nxtcache_c.h"
#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/RSCache.h"
// RSCache.h only forward-declares these, but its inline constructor needs them
// complete to destroy its unique_ptr members.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kLocIndex = 16;
constexpr int kLocShift = 8;
constexpr uint32_t kClipDoor = 0x4000000;
constexpr uint8_t kNoOption = 0xFF;

int failures = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

std::string cachePathFrom(int argc, char **argv)
{
    if (argc > 1)
    {
        return argv[1];
    }
    if (const char *env = std::getenv("NXTCACHE_TEST_CACHE"))
    {
        return env;
    }
    return R"(C:\ProgramData\Jagex\RuneScape)";
}

bool loadLoc(RSCache &cache, int id, LocationType &outLoc)
{
    Archive &archive = cache.archive(kLocIndex, id >> kLocShift);
    if (archive.id == -1)
    {
        return false;
    }
    RSBuffer buffer = archive.readFile(id & ((1 << kLocShift) - 1));
    if (buffer.buffer == nullptr || buffer.remaining() == 0)
    {
        return false;
    }
    outLoc = LocationType{};
    outLoc.id = id;
    outLoc.decode(buffer);
    return true;
}

bool hasNoOptions(const LocationType &loc)
{
    for (const std::string &opt : loc.options)
    {
        if (!opt.empty())
        {
            return false;
        }
    }
    return true;
}

// One placement whose crossing is pinned.
struct DoorPin
{
    const char *label;
    int objectId;    // placed (base) id
    int worldX;
    int worldY;
    int plane;
    bool isMultiloc;
    int activeId;    // the variant a fresh player sees (objectId when not a multiloc)
    int optionSlot;  // its option slot that opens the door
    const char *option;
};

constexpr DoorPin kPins[] = {
    // Barrows tunnels: varbit 4567, transforms {6714, 6715, -1}, default value 0.
    {"Barrows tunnel door (varbit)", 6716, 3534, 9718, 0, true, 6714, 0, "Open"},
    // Barrows tunnels: varbit 4568, same transforms.
    {"Barrows tunnel door (varbit)", 6717, 3528, 9677, 0, true, 6714, 0, "Open"},
    // Varp-only multiloc (varp 2551) south of Lumbridge; active 2406 "Door".
    {"varp multiloc door", 118019, 3201, 3169, 0, true, 2406, 0, "Open"},
    // Ordinary door, no transforms: must be unaffected.
    {"Lumbridge castle door (plain)", 12348, 3207, 3217, 0, false, 12348, 0, "Open"},
};

// The cache still describes the pin the way it did when it was taken.
bool checkPreconditions(RSCache &cpp, const DoorPin &pin)
{
    LocationType placed{};
    LocationType active{};
    if (!loadLoc(cpp, pin.objectId, placed) || !loadLoc(cpp, pin.activeId, active))
    {
        check(false, std::string(pin.label) + ": loc defs readable");
        return false;
    }
    bool isOk = active.options.size() > static_cast<std::size_t>(pin.optionSlot)
             && active.options[pin.optionSlot] == pin.option;
    if (pin.isMultiloc)
    {
        isOk = isOk && hasNoOptions(placed) && !placed.transforms.empty()
            && placed.transforms[0] == pin.activeId;
    }
    else
    {
        isOk = isOk && placed.transforms.empty();
    }
    check(isOk, std::string(pin.label) + " " + std::to_string(pin.objectId)
                    + ": cache matches the pin (base options empty for a multiloc, active "
                    + std::to_string(pin.activeId) + " option " + std::to_string(pin.optionSlot)
                    + " == \"" + pin.option + "\")");
    return isOk;
}

void testPin(nxt_cache *cache, RSCache &cpp, const DoorPin &pin)
{
    std::printf("%s %d at (%d,%d,%d)\n", pin.label, pin.objectId, pin.worldX, pin.worldY, pin.plane);
    if (!checkPreconditions(cpp, pin))
    {
        return;
    }

    const int squareX = pin.worldX >> 6;
    const int squareY = pin.worldY >> 6;
    uint32_t *clip = nullptr;
    size_t clipCount = 0;
    nxt_crossing *crossings = nullptr;
    size_t crossingCount = 0;
    const nxt_result rc = nxt_get_mapsquare_clip_and_crossings(
        cache, squareX, squareY, &clip, &clipCount, nullptr, &crossings, &crossingCount);
    check(rc == NXT_OK && clipCount == 4 * 64 * 64, "square decodes (rc=" + std::to_string(rc) + ")");
    if (rc != NXT_OK)
    {
        return;
    }

    const nxt_crossing *door = nullptr;
    for (size_t i = 0; i < crossingCount; ++i)
    {
        const nxt_crossing &c = crossings[i];
        if (c.object_id == pin.objectId && c.world_x == pin.worldX && c.world_y == pin.worldY
            && c.plane == pin.plane && c.kind == NXT_CROSSING_DOOR)
        {
            door = &c;
        }
    }
    check(door != nullptr, "a DOOR crossing names the placed id at the pinned tile");
    if (door != nullptr)
    {
        check(door->option_index != kNoOption && door->option_index == pin.optionSlot,
              "option_index == " + std::to_string(pin.optionSlot) + " (got "
                  + std::to_string(door->option_index) + "; 255 = no option)");
    }
    const int lx = pin.worldX & 63;
    const int ly = pin.worldY & 63;
    const uint32_t flags = clip[(pin.plane * 64 + lx) * 64 + ly];
    check((flags & kClipDoor) != 0, "CLIP_DOOR set on the tile");

    nxt_free(clip);
    nxt_free(crossings);
}

}  // namespace

int main(int argc, char **argv)
{
    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(std::filesystem::path(path) / "js5-5.jcache"))
    {
        std::printf("SKIP: no cache at %s (js5-5.jcache absent).\n", path.c_str());
        return kExitSkip;
    }
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (cache == nullptr)
    {
        std::printf("FAIL: nxt_cache_open_local(%s): %s\n", path.c_str(), nxt_last_error());
        return 1;
    }
    RSCache cpp(path);
    for (const DoorPin &pin : kPins)
    {
        testPin(cache, cpp, pin);
    }
    nxt_cache_close(cache);
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
