// Tests for the interface component decoder and the stat definitions.
//
//   nxtcache-if-stats-test --offline     synthetic buffers only (no cache, no network)
//   nxtcache-if-stats-test [cache-dir]   a real cache through the public C ABI
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// The real-cache run reads only what the cache holds. A client cache holds only
// the interfaces the client has fetched, so the full-archive and sample checks
// need NXTCACHE_TEST_LIVE=1, which pulls missing archives from live JS5.
// Exit codes: 0 passed, 1 a check failed (including an existing cache directory
// that does not open), 77 no cache directory or nothing checkable (SKIP).
//
// The sample components were read from the 950 cache (2026-10-08). A content
// update can legitimately change one; the full sweep does not depend on content.

#include "c_api/nxtcache_c.h"
#include "config_types/InterfaceTypes.h"
#include "config_types/StatType.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using json = nlohmann::json;
using Bytes = std::vector<uint8_t>;

constexpr int kExitSkip = 77;

int failures = 0;
int skippedChecks = 0;
int ranChecks = 0;

void check(bool isOk, const std::string &what)
{
    ranChecks++;
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

// ------------------------------------------------------------- synthetic ----

void put8(Bytes &b, int v)
{
    b.push_back(static_cast<uint8_t>(v));
}

void put16(Bytes &b, int v)
{
    put8(b, v >> 8);
    put8(b, v);
}

void put24(Bytes &b, int v)
{
    put8(b, v >> 16);
    put16(b, v);
}

void put32(Bytes &b, uint32_t v)
{
    put16(b, static_cast<int>(v >> 16));
    put16(b, static_cast<int>(v & 0xFFFF));
}

void putStr(Bytes &b, const char *s)
{
    while (*s)
    {
        put8(b, *s++);
    }
    put8(b, 0);
}

void putZeros(Bytes &b, int n)
{
    for (int i = 0; i < n; i++)
    {
        put8(b, 0);
    }
}

InterfaceComponentDef decodeComponent(const Bytes &bytes)
{
    std::vector<char> copy(bytes.begin(), bytes.end());
    RSBuffer buffer(copy.data(), copy.size());
    InterfaceComponentDef c;
    c.decode(buffer);
    return c;
}

// Version 0xFF layer: every "version >= N" gate is closed.
Bytes layerV255()
{
    Bytes b;
    put8(b, 0xFF);
    put8(b, 0);                          // type 0, no debug name
    put16(b, 0);                         // subtype
    put16(b, 5); put16(b, 6); put16(b, 16); put16(b, 32);
    putZeros(b, 4);                      // size/position modes
    put16(b, 7);                         // parent
    put8(b, 1);                          // interactFlag: hidden
    put16(b, 1); put16(b, 2);            // layer tail: scroll w/h
    put8(b, 1);                          // version == -1 byte
    putZeros(b, 3);                      // 3-byte event mask (version < 6)
    put8(b, 0);                          // option list end
    put8(b, 0); put8(b, 0); put8(b, 0);  // opBase "", packed counts, opSuffix ""
    putZeros(b, 3);                      // cursor bytes
    put8(b, 0);                          // targetVerb ""
    putZeros(b, 20);                     // 20 hooks (no 38/49-52 below version 0/6/8)
    putZeros(b, 5);                      // 5 trigger lists
    return b;
}

// Version 11 text with a debug name, one op, one cursor, both event tables,
// a hook with an int and a string argument, and one trigger.
Bytes textV11()
{
    Bytes b;
    put8(b, 11);
    put8(b, 0x84);                       // type 4 + debug name
    putStr(b, "nm");
    put16(b, 0);
    putZeros(b, 12);                     // geometry + modes
    put16(b, 0xFFFF);                    // root
    put8(b, 0);
    put16(b, 0x7FFF);                    // text tail: smart -1
    put8(b, 0);                          // version >= 2 flag
    putStr(b, "hi");
    putZeros(b, 4);                      // font size/style/effect, flagBit1
    put32(b, 0x00FF00);                  // colour
    put8(b, 0);                          // ~alpha
    put8(b, 0);                          // shadow (version >= 0)
    put32(b, 0xFFFFFFFF);                // trigger script (version >= 6)
    put8(b, 0);                          // misc byte (version >= 9)
    put32(b, 0);                         // event mask
    put8(b, 0);                          // option list end
    put8(b, 0);                          // opBase ""
    put8(b, 0x11);                       // 1 op, 1 op cursor
    putStr(b, "Talk");
    put8(b, 0); put16(b, 5);             // cursor: index 0 -> 5
    put8(b, 0);                          // opSuffix ""
    putZeros(b, 3);
    put8(b, 0);                          // targetVerb ""
    put16(b, 0xFFFF);                    // event chain (version >= 0)
    put8(b, 1); put24(b, 1); put32(b, 2);
    put8(b, 1); put24(b, 3); put8(b, 0); putStr(b, "s");
    put8(b, 2); put8(b, 0); put32(b, 100); put8(b, 1); putStr(b, "x");  // hook 35
    putZeros(b, 9 + 1 + 10 + 3 + 1);     // rest of block A, 38, block B, 49-51, 52
    put8(b, 1); put32(b, 123);           // trigger list 0
    putZeros(b, 4);
    return b;
}

void testSyntheticComponents()
{
    std::printf("synthetic interface components\n");
    InterfaceComponentDef layer = decodeComponent(layerV255());
    check(layer.isDecoded && layer.bytesConsumed == layer.fileLength, "v255 layer consumed exactly");
    check(layer.version == -1 && layer.componentType == 0 && layer.parentScopeId == 7 && layer.isHidden,
          "v255 layer: version -1, type 0, parent 7, hidden");
    check(layer.rawWidth == 16 && layer.layerScrollWidth == 1 && layer.layerFlagByte == 1, "v255 layer geometry + tail");

    InterfaceComponentDef text = decodeComponent(textV11());
    check(text.isDecoded, "v11 text decoded (" + text.decodeError + ")");
    check(text.debugName == "nm" && text.text.text == "hi" && text.text.textId == -1, "v11 text name/string/smart");
    check(text.ops == std::vector<std::string>{"Talk"} && text.opCursors.size() == 1 &&
          text.opCursors[0].cursor == 5, "v11 ops + op cursor");
    check(text.eventScripts.size() == 1 && text.eventStrings.size() == 1 && text.eventStrings[0].value == "s",
          "v11 both event tables");
    check(text.hooks.size() == 1 && text.hooks[0].hookId == 35 && text.hooks[0].scriptId == 100 &&
          text.hooks[0].args.size() == 1 && text.hooks[0].args[0].stringValue == "x", "v11 hook 35 with string arg");
    check(text.triggers[0] == std::vector<uint32_t>{123}, "v11 trigger list 0");

    for (const Bytes &valid : {layerV255(), textV11()})
    {
        int undetected = 0;
        for (size_t n = 0; n < valid.size(); n++)
        {
            InterfaceComponentDef c = decodeComponent(Bytes(valid.begin(), valid.begin() + static_cast<long>(n)));
            if (c.isDecoded || c.decodeError.empty())
            {
                undetected++;
            }
        }
        check(undetected == 0, "every truncated prefix (" + std::to_string(valid.size()) + ") flags decodeError");
        Bytes longer = valid;
        put8(longer, 0);
        InterfaceComponentDef c = decodeComponent(longer);
        check(!c.isDecoded && c.decodeError.find("decoded") == 0, "over-long buffer flagged: " + c.decodeError);
    }

    InterfaceComponentDef unknown = decodeComponent(Bytes{0xFF, 0x01, 0, 0, 0, 0});
    check(!unknown.isDecoded && unknown.decodeError.find("unknown component type 1") == 0, "unknown type flagged");
    InterfaceComponentDef noNul = decodeComponent(Bytes{0xFF, 0x80, 'a', 'b'});
    check(!noNul.isDecoded && noNul.decodeError == "truncated preamble", "unterminated debug name flagged");
}

Bytes statsGroup(int badTableIndex)
{
    Bytes b;
    put8(b, 2); put8(b, 1);              // opcode 2: one table
    put8(b, 0); put16(b, 2); put32(b, 0); put32(b, 100);
    put8(b, 0xFF);
    put8(b, 1); put8(b, badTableIndex >= 0 ? 3 : 2);  // opcode 1
    put8(b, 0); put16(b, 99); put8(b, 0); put8(b, 1); // stat 0: default table
    put8(b, 5); put16(b, 10); put8(b, 0x0F);          // stat 5: cap, table 0, base 0
    put8(b, 3); put8(b, 0); put8(b, 0); put8(b, 0);
    if (badTableIndex >= 0)
    {
        put8(b, 6); put16(b, 50); put8(b, 4); put8(b, badTableIndex); put8(b, 0);
    }
    put8(b, 0);
    return b;
}

bool decodeStats(const Bytes &bytes, StatDefaults &out, std::string &outError)
{
    std::vector<char> copy(bytes.begin(), bytes.end());
    RSBuffer buffer(copy.data(), copy.size());
    try
    {
        out.decode(buffer);
        return true;
    }
    catch (const std::runtime_error &e)
    {
        outError = e.what();
        return false;
    }
}

void testSyntheticStats()
{
    std::printf("synthetic stat definitions\n");
    StatDefaults defs;
    std::string error;
    check(decodeStats(statsGroup(-1), defs, error), "valid group decodes");
    const StatType *s0 = defs.find(0);
    const StatType *s5 = defs.find(5);
    check(s0 && s0->baseLevel == 1 && defs.xpTableFor(*s0).size() == 120, "stat 0: base 1, default table");
    check(s0 && defs.levelForXp(*s0, 82) == 1 && defs.levelForXp(*s0, 83) == 2, "stat 0 level rule");
    check(s5 && s5->baseLevel == 0 && s5->capLevel == 3 && s5->capXpTenths == 1000, "stat 5: base 0, cap 3, cap xp 100.0");
    check(s5 && defs.levelForXp(*s5, 0) == 1 && defs.levelForXp(*s5, 100) == 2 &&
          defs.levelForXp(*s5, 1000000) == 2, "stat 5 levels against its 2-entry table");
    check(defs.trailingBytes == 0, "no trailing bytes");

    StatDefaults bad;
    check(decodeStats(statsGroup(7), bad, error), "group with a bad table index decodes");
    const StatType *s6 = bad.find(6);
    check(s6 && s6->hasInvalidXpTableIndex && bad.xpTableFor(*s6).size() == 120,
          "bad table index flagged, default table used");

    Bytes valid = statsGroup(-1);
    int undetected = 0;
    for (size_t n = 0; n < valid.size(); n++)
    {
        StatDefaults partial;
        partial.trailingBytes = -5;
        std::string e;
        bool isOk = decodeStats(Bytes(valid.begin(), valid.begin() + static_cast<long>(n)), partial, e);
        if (isOk || e.find("truncated") == std::string::npos || partial.trailingBytes != -5)
        {
            undetected++;
        }
    }
    check(undetected == 0, "every truncated prefix (" + std::to_string(valid.size()) +
                               ") throws and leaves the object untouched");
    Bytes longer = valid;
    put8(longer, 0xAB);
    put8(longer, 0xCD);
    StatDefaults extra;
    check(decodeStats(longer, extra, error) && extra.trailingBytes == 2, "over-long buffer: 2 trailing bytes recorded");
}

// ------------------------------------------------------------ real cache ----

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

bool fetchInterface(nxt_cache *cache, int id, json &outDoc)
{
    char *text = nullptr;
    size_t len = 0;
    if (nxt_get_if_json(cache, id, &text, &len) != NXT_OK)
    {
        return false;
    }
    outDoc = json::parse(std::string(text, len));
    nxt_free(text);
    return true;
}

const json *component(const json &doc, int id)
{
    const auto &comps = doc["components"];
    auto it = comps.find(std::to_string(id));
    return it == comps.end() ? nullptr : &*it;
}

bool hasHook(const json &c, int hookId, int scriptId)
{
    for (const auto &h : c["hooks"])
    {
        if (h["hookId"] == hookId && h["scriptId"] == scriptId)
        {
            return true;
        }
    }
    return false;
}

nxt_stat_info statInfo(nxt_cache *cache, int id, nxt_result &outRc)
{
    nxt_stat_info info{};
    info.struct_size = sizeof(nxt_stat_info);
    outRc = nxt_get_stat_info(cache, id, &info);
    return info;
}

void expectLevel(nxt_cache *cache, int stat, uint32_t xp, int level)
{
    int32_t got = -1;
    nxt_result rc = nxt_stat_level_for_xp(cache, stat, xp, &got);
    check(rc == NXT_OK && got == level, "stat " + std::to_string(stat) + " xp " + std::to_string(xp) +
                                            " -> level " + std::to_string(got) + " (want " +
                                            std::to_string(level) + ")");
}

void testCacheStats(nxt_cache *cache)
{
    std::printf("stat definitions (defaults 28/9)\n");
    nxt_result rc = NXT_OK;
    nxt_stat_info attack = statInfo(cache, 0, rc);
    if (rc == NXT_ERR_IO)
    {
        skipCheck(std::string("group 28/9 not in this cache: ") + nxt_last_error());
        return;
    }
    check(rc == NXT_OK && attack.version == NXT_STAT_INFO_VERSION && attack.base_level == 1, "stat 0: base level 1");
    check(attack.xp_table_index == -1 && attack.xp_table_length == 120 && attack.xp_table_index_invalid == 0,
          "stat 0 uses the 120-entry default table");

    uint32_t *table = nullptr;
    size_t n = 0;
    check(nxt_get_stat_xp_table(cache, 0, &table, &n) == NXT_OK && n == 120, "default table length 120");
    if (table && n == 120)
    {
        check(table[0] == 83 && table[97] == 13034431, "default table: level 2 = 83, level 99 = 13034431");
    }
    nxt_free(table);
    expectLevel(cache, 0, 0, 1);
    expectLevel(cache, 0, 82, 1);
    expectLevel(cache, 0, 83, 2);
    expectLevel(cache, 0, 13034431, 99);

    nxt_stat_info herblore = statInfo(cache, 15, rc);
    check(rc == NXT_OK && herblore.cap_level == 5 && herblore.cap_xp_tenths == 3880, "stat 15: cap 5, cap xp 388.0");
    nxt_stat_info invention = statInfo(cache, 26, rc);
    check(rc == NXT_OK && invention.xp_table_index == 0 && invention.xp_table_length == 150 &&
          invention.base_level == 0, "stat 26: table 0 (150 entries), base level 0");
    expectLevel(cache, 26, 829, 1);
    expectLevel(cache, 26, 830, 2);

    statInfo(cache, 200, rc);
    check(rc == NXT_ERR_NOT_FOUND, "stat 200 -> NXT_ERR_NOT_FOUND");
    nxt_stat_info small{};
    small.struct_size = 4;
    check(nxt_get_stat_info(cache, 0, &small) == NXT_ERR_INVALID, "short struct_size -> NXT_ERR_INVALID");

    char *text = nullptr;
    size_t len = 0;
    check(nxt_get_stats_json(cache, &text, &len) == NXT_OK, "stats json");
    if (text)
    {
        check(json::parse(std::string(text, len))["stats"].size() >= 29, "stats json lists >= 29 stats");
        nxt_free(text);
    }
}

void testSamples(nxt_cache *cache)
{
    std::printf("sample components (1188 dialog, 517 bank, 1430 action bar, 105 input)\n");
    json dialog;
    json bank;
    json bar;
    json input;
    if (!fetchInterface(cache, 1188, dialog) || !fetchInterface(cache, 517, bank) ||
        !fetchInterface(cache, 1430, bar) || !fetchInterface(cache, 105, input))
    {
        skipCheck("a sample interface is not in this cache (set NXTCACHE_TEST_LIVE=1)");
        return;
    }
    const json *root = component(dialog, 5);
    const json *opt1 = component(dialog, 7);
    const json *close = component(dialog, 4);
    check(root && (*root)["parentId"] == -1 && hasHook(*root, 35, 8420) &&
          (*root)["hooks"][0]["args"][0]["int"] == (1188 << 16 | 1), "1188:5 root layer, hook 35 -> 8420(1188:1, ...)");
    check(opt1 && (*opt1)["text"]["text"] == "1." && (*opt1)["parentId"] == 12, "1188:7 text '1.', parent 12");
    check(close && close->at("ops") == json::array({"Close"}), "1188:4 ops [Close]");
    const json *deposit = component(bank, 39);
    const json *pouch = component(bank, 18);
    check(deposit && (*deposit)["sprite"]["spriteId"] == 7664 && (*deposit)["parentId"] == 38 &&
          deposit->at("ops") == json::array({"Deposit carried items"}) && hasHook(*deposit, 5, 10009),
          "517:39 sprite 7664, parent 38, ops, tooltip hook");
    check(pouch && (*pouch)["text"]["text"].get<std::string>().rfind("Coin Pouch", 0) == 0 &&
          (*pouch)["triggers"][1] == json::array({623}), "517:18 text 'Coin Pouch...', trigger list 1 = [623]");
    const json *regen = component(bar, 13);
    check(regen && (*regen)["sprite"]["spriteId"] == 18851 &&
          regen->at("ops") == json::array({"Regenerate", "Cure Poison"}), "1430:13 sprite 18851, ops");
    const json *field = component(input, 225);
    check(field && (*field)["typeName"] == "input" && (*field)["agentType"] == 12 &&
          (*field)["input"]["hover"]["leftX"] == 16, "105:225 input (agent 12), hover record aligned");
}

void testFullArchive(nxt_cache *cache, bool isLive)
{
    std::printf("interface archive sweep (%s)\n", isLive ? "live fallback" : "local archives only");
    int *ids = nullptr;
    size_t count = 0;
    check(nxt_list_type_ids(cache, "if", &ids, &count) == NXT_OK, "list interface ids");
    int interfaces = 0;
    int components = 0;
    int failed = 0;
    int absent = 0;
    for (size_t i = 0; i < count; i++)
    {
        json doc;
        if (!fetchInterface(cache, ids[i], doc))
        {
            absent++;
            continue;
        }
        interfaces++;
        for (auto &[key, c] : doc["components"].items())
        {
            components++;
            if (c.contains("decodeError"))
            {
                if (failed++ < 5)
                {
                    std::printf("    %d:%s %s\n", ids[i], key.c_str(), c["decodeError"].get<std::string>().c_str());
                }
            }
        }
    }
    nxt_free(ids);
    std::printf("  %d interfaces, %d components, %d not in the cache\n", interfaces, components, absent);
    if (interfaces == 0)
    {
        skipCheck("no interface archive is in this cache (set NXTCACHE_TEST_LIVE=1)");
        return;
    }
    check(failed == 0, "0 components with a decode error (" + std::to_string(failed) + ")");
    if (isLive)
    {
        check(absent == 0 && interfaces >= 1800, "every interface readable, >= 1800");
    }
}

int runCache(int argc, char **argv)
{
    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(path))
    {
        std::printf("SKIPPED: cache directory %s not found\n", path.c_str());
        return kExitSkip;
    }
    // nxt_cache_open_local opens lazily, so check the shape here: an existing
    // path that is not a jcache directory is a misconfiguration, not a skip.
    bool hasJcache = false;
    if (std::filesystem::is_directory(path))
    {
        for (const auto &entry : std::filesystem::directory_iterator(path))
        {
            const std::string name = entry.path().filename().string();
            // Non-empty only: opening a local cache creates empty js5-N.jcache
            // files for every index it touches.
            hasJcache = hasJcache || (name.rfind("js5-", 0) == 0 && entry.path().extension() == ".jcache" &&
                                      entry.is_regular_file() && entry.file_size() > 0);
        }
    }
    if (!hasJcache)
    {
        std::printf("FAIL: %s exists but is not a directory of js5-*.jcache files\n", path.c_str());
        return 1;
    }
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (!cache)
    {
        std::printf("FAIL: cache directory %s exists but does not open: %s\n", path.c_str(), nxt_last_error());
        return 1;
    }
    testCacheStats(cache);
    const char *live = std::getenv("NXTCACHE_TEST_LIVE");
    const bool isLive = live != nullptr && std::strcmp(live, "1") == 0;
    if (isLive)
    {
        check(nxt_cache_enable_live_fallback(cache) == NXT_OK, "live JS5 fallback enabled");
    }
    testSamples(cache);
    testFullArchive(cache, isLive);
    nxt_cache_close(cache);
    // Three sections (stats, samples, sweep); if all three were skipped the
    // cache held nothing to test, which is a SKIP rather than a pass.
    constexpr int kSections = 3;
    if (failures == 0 && skippedChecks >= kSections)
    {
        std::printf("SKIPPED: nothing checkable in %s\n", path.c_str());
        return kExitSkip;
    }
    std::printf("%s: %d failure(s), %d skipped check(s)\n", failures == 0 ? "PASS" : "FAIL", failures, skippedChecks);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--offline") == 0)
    {
        testSyntheticComponents();
        testSyntheticStats();
        std::printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
        return failures == 0 ? 0 : 1;
    }
    return runCache(argc, argv);
}
