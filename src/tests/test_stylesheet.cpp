// Stylesheet (JS5 index 60) decoder tests, and the interface field that names
// a stylesheet (InterfaceComponentDef::styleSheetId).
//
//   nxtcache-stylesheet-test --offline      hand-built sheets, no cache
//   nxtcache-stylesheet-test [cache-dir]    every group of a real cache
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// The real-cache run decodes every group and requires each to decode with no
// byte left over. Its value pins (NXTLibrary claudedocs/plugin-ui-p2-map-951-4.md
// 15.4) run only on the pinned 951-4 index-60 and index-3 versions and print
// SKIPPED CHECK otherwise, so a newer cache cannot pass them by accident.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP).

#include "config_types/InterfaceTypes.h"
#include "config_types/StyleSheetType.h"
#include "core/RSBuffer.h"
#include "core/RSCache.h"
// RSCache's destructor needs the JS5 types it owns by unique_ptr complete.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kStyleSheetIndex = 60;
constexpr int kInterfaceIndex = 3;
// Reference-table versions of the 951-4 cache the pins were taken from.
constexpr int kPinnedIndex60Version = 1791826941;
constexpr int kPinnedIndex3Version = 1794589490;

int failures = 0;
int skippedChecks = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

void skipCheck(const std::string &what)
{
    std::printf("  SKIPPED CHECK  %s\n", what.c_str());
    skippedChecks++;
}

// --- Fixture builder -------------------------------------------------------

struct FixtureEntry
{
    int valueType;
    int32_t keyHash;
    int32_t value;
};

void putU16(std::vector<char> &out, int v)
{
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}

void putI32(std::vector<char> &out, int32_t v)
{
    const auto u = static_cast<uint32_t>(v);
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<char>((u >> shift) & 0xFF));
    }
}

std::vector<char> sheetBytes(int parentId, const std::vector<FixtureEntry> &entries)
{
    std::vector<char> out;
    putU16(out, parentId & 0xFFFF);
    putU16(out, static_cast<int>(entries.size()));
    for (const FixtureEntry &e : entries)
    {
        out.push_back(static_cast<char>(e.valueType));
        putI32(out, e.keyHash);
        putI32(out, e.value);
    }
    return out;
}

StyleSheetType decodeBytes(int id, std::vector<char> bytes)
{
    RSBuffer buffer(bytes.data(), bytes.size());
    buffer.writePosition = static_cast<unsigned int>(bytes.size());
    StyleSheetType sheet;
    sheet.id = id;
    sheet.decode(buffer);
    return sheet;
}

int32_t resolvedValue(const std::map<int, StyleSheetType> &sheets, int id, const char *key,
                      int &outFrom)
{
    const auto rv = resolveStyleSheetValue(sheets, id, styleSheetKeyHash(key));
    outFrom = rv ? rv->sourceId : -2;
    return rv ? rv->value : INT32_MIN;
}

// --- Offline ---------------------------------------------------------------

void testHash()
{
    std::printf("key hash\n");
    // Java: "".hashCode() == 0, "hello".hashCode() == 99162322,
    // "polygenelubricants".hashCode() == Integer.MIN_VALUE.
    static_assert(styleSheetKeyHash("") == 0);
    static_assert(styleSheetKeyHash("hello") == 99162322);
    static_assert(styleSheetKeyHash("polygenelubricants") == INT32_MIN);
    check(styleSheetKeyHash("input.sprite") == 596498793, "input.sprite hashes to 596498793");

    std::set<int32_t> hashes;
    size_t roundTrips = 0;
    // The table is private; walk it through the hashes of the names it holds.
    for (const char *key : {"input.sprite", "input.sprite.alpha", "input.caret.rgb",
                            "input.text.font", "button.sprite", "slider.track.sprite.enabletiling"})
    {
        const char *name = styleSheetKeyName(styleSheetKeyHash(key));
        if (name != nullptr && std::string(name) == key)
        {
            roundTrips++;
        }
        hashes.insert(styleSheetKeyHash(key));
    }
    check(roundTrips == 6, "known names round-trip through styleSheetKeyName");
    check(styleSheetKeyName(styleSheetKeyHash("input.tint.rgb")) == nullptr,
          "a name no 951-4 sheet uses is not in the table (input.tint.rgb)");
    check(styleSheetKeyName(12345) == nullptr, "an unknown hash has no name");
    check(styleSheetKnownKeyCount() == 214, "214 known key names");
}

void testDecode()
{
    std::printf("decode\n");
    const int32_t sprite = styleSheetKeyHash("input.sprite");
    const int32_t edgeH = styleSheetKeyHash("input.sprite.edge.horizontal");

    const StyleSheetType a = decodeBytes(10, sheetBytes(-1, {{0, sprite, 100}, {0, edgeH, 4}, {0, edgeH, 12}}));
    check(a.isDecodeOk && a.parentId == -1 && a.entries.size() == 3 && a.trailingBytes == 0,
          "three entries, parent -1, every byte consumed");
    check(a.find(edgeH) == 4, "a duplicate key keeps the first occurrence");
    check(!a.find(styleSheetKeyHash("input.text.font")).has_value(), "an absent key is nullopt");

    const StyleSheetType neg = decodeBytes(11, sheetBytes(-1, {{0, sprite, -5}}));
    check(neg.isDecodeOk && neg.find(sprite) == -5, "values are signed i32");

    std::vector<char> bad = sheetBytes(55, {{0, sprite, 1}, {1, edgeH, 2}});
    const StyleSheetType b = decodeBytes(12, bad);
    check(!b.isDecodeOk && b.badValueType == 1 && b.entries.empty() && b.parentId == -1,
          "a non-zero valueType fails the sheet and leaves no partial state");

    std::vector<char> truncated = sheetBytes(55, {{0, sprite, 1}, {0, edgeH, 2}});
    truncated.resize(truncated.size() - 3);
    const StyleSheetType t = decodeBytes(13, truncated);
    check(!t.isDecodeOk && t.entries.empty() && t.parentId == -1, "a truncated sheet fails");

    std::vector<char> tail = sheetBytes(55, {{0, sprite, 1}});
    tail.push_back(0);
    const StyleSheetType tl = decodeBytes(14, tail);
    check(tl.isDecodeOk && tl.trailingBytes == 1 && tl.parentId == 55, "trailing bytes are counted");

    const StyleSheetType empty = decodeBytes(15, sheetBytes(7, {}));
    check(empty.isDecodeOk && empty.entries.empty() && empty.parentId == 7, "an empty sheet decodes");
}

void testResolve()
{
    std::printf("resolve\n");
    const int32_t sprite = styleSheetKeyHash("input.sprite");
    const int32_t edgeH = styleSheetKeyHash("input.sprite.edge.horizontal");
    const int32_t font = styleSheetKeyHash("input.text.font");

    std::map<int, StyleSheetType> sheets;
    sheets[10] = decodeBytes(10, sheetBytes(-1, {{0, sprite, 100}, {0, edgeH, 4}, {0, edgeH, 12}}));
    sheets[11] = decodeBytes(11, sheetBytes(10, {{0, sprite, 200}}));
    sheets[12] = decodeBytes(12, sheetBytes(11, {{0, font, 26}}));
    sheets[20] = decodeBytes(20, sheetBytes(21, {{0, font, 1}}));
    sheets[21] = decodeBytes(21, sheetBytes(20, {{0, font, 2}}));
    sheets[30] = decodeBytes(30, sheetBytes(99, {{0, font, 3}}));
    sheets[40] = decodeBytes(40, sheetBytes(-1, {{0, sprite, 1}, {7, edgeH, 2}}));
    sheets[41] = decodeBytes(41, sheetBytes(40, {}));

    int from = 0;
    check(resolvedValue(sheets, 12, "input.sprite", from) == 200 && from == 11, "child of a child: nearest parent wins");
    check(resolvedValue(sheets, 12, "input.sprite.edge.horizontal", from) == 4 && from == 10,
          "grandparent supplies a key, first duplicate");
    check(resolvedValue(sheets, 12, "input.text.font", from) == 26 && from == 12, "own value wins");
    check(resolvedValue(sheets, 10, "input.text.font", from) == INT32_MIN, "a key nowhere in the chain is absent");
    check(resolvedValue(sheets, 20, "input.sprite", from) == INT32_MIN, "a parent cycle terminates");
    check(resolvedValue(sheets, 30, "input.text.font", from) == 3, "a missing parent ends the chain");
    check(resolvedValue(sheets, 41, "input.sprite", from) == INT32_MIN, "a failed parent counts as absent");
    check(resolvedValue(sheets, 999, "input.sprite", from) == INT32_MIN, "an unknown sheet is absent");

    const auto all = resolveStyleSheet(sheets, 12);
    check(all.size() == 3 && all.at(sprite).value == 200 && all.at(edgeH).value == 4 && all.at(font).sourceId == 12,
          "resolveStyleSheet: own, parent and grandparent keys, nearest first");
}

int runOffline()
{
    testHash();
    testDecode();
    testResolve();
    std::printf("%s (%d failed)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

// --- Real cache ------------------------------------------------------------

std::map<int, StyleSheetType> decodeAll(RSCache &cache, int &outGroups, int &outMissingFile)
{
    std::map<int, StyleSheetType> sheets;
    outGroups = 0;
    outMissingFile = 0;
    for (int aid : cache.archiveIds(kStyleSheetIndex))
    {
        outGroups++;
        auto &archive = cache.archive(kStyleSheetIndex, aid);
        RSBuffer buffer = archive.id == -1 ? RSBuffer(nullptr, 0) : archive.readFile(0);
        if (buffer.buffer == nullptr)
        {
            outMissingFile++;
            continue;
        }
        StyleSheetType sheet;
        sheet.id = aid;
        sheet.decode(buffer);
        sheets.emplace(aid, std::move(sheet));
    }
    return sheets;
}

void checkSweep(const std::map<int, StyleSheetType> &sheets, int groups, int missingFile)
{
    int okCount = 0;
    int withTail = 0;
    int badType = 0;
    std::set<int32_t> hashes;
    for (const auto &[id, s] : sheets)
    {
        okCount += s.isDecodeOk ? 1 : 0;
        withTail += s.trailingBytes != 0 ? 1 : 0;
        badType += s.badValueType != -1 ? 1 : 0;
        for (const StyleSheetEntry &e : s.entries)
        {
            hashes.insert(e.keyHash);
        }
    }
    size_t named = 0;
    for (int32_t h : hashes)
    {
        named += styleSheetKeyName(h) != nullptr ? 1 : 0;
    }
    std::printf("  %d groups, %d decoded, %zu distinct key hashes, %zu named\n", groups, okCount, hashes.size(), named);
    check(missingFile == 0, "every group has a file 0");
    check(okCount == groups && badType == 0, "every group decodes, no non-zero valueType");
    check(withTail == 0, "every group consumes every byte");
    check(groups > 0, "index 60 is not empty");
    check(named == styleSheetKnownKeyCount(), "every known key name is used by some sheet");
}

void checkPinnedSheets(const std::map<int, StyleSheetType> &sheets)
{
    check(sheets.size() == 235, "235 stylesheets");
    struct Pin
    {
        int id;
        int parent;
        int32_t sprite;
    };
    for (const Pin &p : {Pin{117, 55, 15505}, Pin{118, 56, 15506}, Pin{119, 55, 15505}, Pin{399, 55, 35540},
                         Pin{60, 55, 15107}})
    {
        int from = 0;
        const auto it = sheets.find(p.id);
        check(it != sheets.end() && it->second.parentId == p.parent,
              "sheet " + std::to_string(p.id) + " parent " + std::to_string(p.parent));
        check(resolvedValue(sheets, p.id, "input.sprite", from) == p.sprite,
              "sheet " + std::to_string(p.id) + " input.sprite = " + std::to_string(p.sprite));
    }
    int from = 0;
    check(resolvedValue(sheets, 60, "input.sprite.alpha", from) == 0, "sheet 60 input.sprite.alpha = 0");
    // 55 lists input.sprite.edge.horizontal as 4, then 12: the first wins.
    const auto s55 = sheets.find(55);
    const int32_t edgeH = styleSheetKeyHash("input.sprite.edge.horizontal");
    int occurrences = 0;
    if (s55 != sheets.end())
    {
        for (const StyleSheetEntry &e : s55->second.entries)
        {
            occurrences += e.keyHash == edgeH ? 1 : 0;
        }
    }
    check(occurrences == 2 && s55->second.find(edgeH) == 4, "sheet 55 duplicate edge.horizontal: 4 then 12, 4 wins");
}

int componentStyleSheet(RSCache &cache, int iface, int component)
{
    auto &archive = cache.archive(kInterfaceIndex, iface);
    if (archive.id == -1)
    {
        return INT32_MIN;
    }
    RSBuffer buffer = archive.readFile(component);
    if (buffer.buffer == nullptr)
    {
        return INT32_MIN;
    }
    InterfaceComponentDef c;
    c.id = component;
    c.decode(buffer);
    return c.styleSheetId;
}

// The 11 Input components of 951-4 (plugin-ui-p2-map-951-4.md 12.4): 399 on
// eight, 117 on 1401:42, 118 on 1442:29, -1 on 1578:11.
void checkPinnedComponents(RSCache &cache, const std::map<int, StyleSheetType> &sheets)
{
    struct Pin
    {
        int iface;
        int component;
        int styleSheetId;
    };
    const Pin pins[] = {
        {105, 225, 399}, {365, 2, 399}, {707, 15, 399}, {1401, 42, 117}, {1442, 29, 118}, {1475, 32, 399},
        {1479, 13, 399}, {1498, 6, 399}, {1578, 11, -1}, {1586, 17, 399}, {1604, 15, 399},
    };
    int matched = 0;
    for (const Pin &p : pins)
    {
        const int got = componentStyleSheet(cache, p.iface, p.component);
        if (got == p.styleSheetId)
        {
            matched++;
        }
        else
        {
            std::printf("    %d:%d styleSheetId %d, expected %d\n", p.iface, p.component, got, p.styleSheetId);
        }
        if (got >= 0)
        {
            check(sheets.count(got) == 1, std::to_string(p.iface) + ":" + std::to_string(p.component) +
                                              " styleSheetId " + std::to_string(got) + " is a decoded stylesheet");
        }
    }
    check(matched == 11, "11 Input components carry the 951-4 styleSheetIds");
}

// Every component of every interface: styleSheetId is -1 or a decoded sheet.
// This is what makes the field a stylesheet id rather than a script reference,
// checked across the whole cache instead of the 11 pinned components.
void checkAllComponents(RSCache &cache, const std::map<int, StyleSheetType> &sheets)
{
    long components = 0;
    long withSheet = 0;
    long unresolved = 0;
    std::map<int, long> byId;
    for (int iface : cache.archiveIds(kInterfaceIndex))
    {
        auto &archive = cache.archive(kInterfaceIndex, iface);
        if (archive.id == -1)
        {
            continue;
        }
        for (auto &[fid, fh] : archive.files)
        {
            RSBuffer buffer = archive.readFile(fid);
            if (buffer.buffer == nullptr || buffer.remaining() == 0)
            {
                continue;
            }
            InterfaceComponentDef c;
            c.id = fid;
            c.decode(buffer);
            components++;
            if (c.styleSheetId == -1)
            {
                continue;
            }
            withSheet++;
            byId[c.styleSheetId]++;
            if (sheets.count(c.styleSheetId) == 0)
            {
                unresolved++;
                if (unresolved <= 5)
                {
                    std::printf("    %d:%d styleSheetId %d is not a stylesheet\n", iface, fid, c.styleSheetId);
                }
            }
        }
        cache.evictArchive(kInterfaceIndex, iface);
    }
    std::printf("  %ld components, %ld with a stylesheet, %zu distinct ids\n", components, withSheet, byId.size());
    check(components > 0, "index 3 has components");
    check(withSheet > 0, "some components name a stylesheet");
    check(unresolved == 0, "every non-(-1) styleSheetId is a decoded stylesheet");
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

int runCache(const std::string &cachePath)
{
    // Check first: opening an index with no file makes sqlite create one.
    const std::filesystem::path dir(cachePath);
    if (!std::filesystem::exists(dir / "js5-60.jcache") || !std::filesystem::exists(dir / "js5-3.jcache"))
    {
        std::printf("SKIP: no js5-60.jcache / js5-3.jcache in %s\n", cachePath.c_str());
        return kExitSkip;
    }
    RSCache cache(cachePath);
    const int v60 = cache.indexVersion(kStyleSheetIndex);
    const int v3 = cache.indexVersion(kInterfaceIndex);
    std::printf("stylesheet cache %s (index 60 v%d, index 3 v%d; pins taken at v%d / v%d)\n", cachePath.c_str(), v60,
                v3, kPinnedIndex60Version, kPinnedIndex3Version);

    int groups = 0;
    int missingFile = 0;
    const std::map<int, StyleSheetType> sheets = decodeAll(cache, groups, missingFile);
    checkSweep(sheets, groups, missingFile);

    checkAllComponents(cache, sheets);

    if (v60 == kPinnedIndex60Version)
    {
        checkPinnedSheets(sheets);
    }
    else
    {
        skipCheck("951-4 stylesheet pins (index 60 is v" + std::to_string(v60) + ")");
    }
    if (v60 == kPinnedIndex60Version && v3 == kPinnedIndex3Version)
    {
        checkPinnedComponents(cache, sheets);
    }
    else
    {
        skipCheck("951-4 Input component pins (index 3 is v" + std::to_string(v3) + ")");
    }
    std::printf("%s (%d failed, %d skipped)\n", failures == 0 ? "PASS" : "FAIL", failures, skippedChecks);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--offline")
    {
        return runOffline();
    }
    return runCache(cachePathFrom(argc, argv));
}
