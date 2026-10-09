// Ground-truth tests for the interface component decoder and the stat
// definitions, run against a real cache through the public C ABI only.
//
//   nxtcache-if-stats-test [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// A client cache holds only the interfaces the client has fetched, so archives
// missing locally are pulled from live JS5 (nxt_cache_enable_live_fallback).
// Exit codes: 0 all checks passed, 1 a check failed, 77 no cache reachable (SKIP).
//
// The sample components below were read from the 950 cache (2026-10-08). A
// content update can legitimately change one; the full-archive sweep is the
// format check and does not depend on content.

#include "c_api/nxtcache_c.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace
{

using json = nlohmann::json;

constexpr int kExitSkip = 77;

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

// Every interface: no decode error, i.e. every component was read to exactly
// its file length with no overrun, and only known types appear.
void testFullArchive(nxt_cache *cache)
{
    std::printf("full interface archive\n");
    int *ids = nullptr;
    size_t count = 0;
    check(nxt_list_type_ids(cache, "if", &ids, &count) == NXT_OK, "list interface ids");
    int interfaces = 0;
    int components = 0;
    int failed = 0;
    int unreadable = 0;
    std::map<int, int> versions;
    for (size_t i = 0; i < count; i++)
    {
        json doc;
        if (!fetchInterface(cache, ids[i], doc))
        {
            unreadable++;
            continue;
        }
        interfaces++;
        for (auto &[key, c] : doc["components"].items())
        {
            components++;
            versions[c["version"].get<int>()]++;
            if (c.contains("decodeError"))
            {
                if (failed < 5)
                {
                    std::printf("    %d:%s %s\n", ids[i], key.c_str(),
                                c["decodeError"].get<std::string>().c_str());
                }
                failed++;
            }
        }
    }
    nxt_free(ids);
    std::printf("  %d interfaces, %d components\n", interfaces, components);
    check(interfaces >= 1800, "at least 1800 interfaces decoded");
    check(unreadable == 0, "every listed interface readable (" + std::to_string(unreadable) + " not)");
    check(failed == 0, "0 components with a decode error (" + std::to_string(failed) + ")");
    check(versions[-1] > 0, "version 0xFF components are decoded (" + std::to_string(versions[-1]) + ")");
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

void testDialog(nxt_cache *cache)
{
    std::printf("interface 1188 (dialog options)\n");
    json doc;
    if (!fetchInterface(cache, 1188, doc))
    {
        check(false, "1188 readable");
        return;
    }
    const json *root = component(doc, 5);
    const json *opt1 = component(doc, 7);
    const json *close = component(doc, 4);
    check(root && (*root)["parentId"] == -1 && (*root)["typeName"] == "layer", "1188:5 is a root layer");
    check(root && hasHook(*root, 35, 8420), "1188:5 hook 35 -> script 8420");
    check(root && (*root)["hooks"][0]["args"][0]["int"] == (1188 << 16 | 1), "1188:5 hook arg 0 = component 1188:1");
    check(opt1 && (*opt1)["typeName"] == "text" && (*opt1)["text"]["text"] == "1.", "1188:7 text '1.'");
    check(opt1 && (*opt1)["parentId"] == 12, "1188:7 parent 12");
    check(close && close->at("ops") == json::array({"Close"}), "1188:4 ops [Close]");
}

void testBank(nxt_cache *cache)
{
    std::printf("interface 517 (bank)\n");
    json doc;
    if (!fetchInterface(cache, 517, doc))
    {
        check(false, "517 readable");
        return;
    }
    const json *deposit = component(doc, 39);
    const json *pouch = component(doc, 18);
    check(deposit && (*deposit)["typeName"] == "sprite" && (*deposit)["sprite"]["spriteId"] == 7664,
          "517:39 sprite 7664");
    check(deposit && deposit->at("ops") == json::array({"Deposit carried items"}), "517:39 ops [Deposit carried items]");
    check(deposit && (*deposit)["parentId"] == 38, "517:39 parent 38");
    check(deposit && hasHook(*deposit, 5, 10009), "517:39 hook 5 -> script 10009 (tooltip)");
    check(pouch && (*pouch)["typeName"] == "text" &&
          (*pouch)["text"]["text"].get<std::string>().rfind("Coin Pouch", 0) == 0, "517:18 text 'Coin Pouch...'");
    check(pouch && (*pouch)["triggers"][1] == json::array({623}), "517:18 trigger list 1 = [623]");
}

void testActionBarAndInput(nxt_cache *cache)
{
    std::printf("interface 1430 (action bar) and 105 (input)\n");
    json bar;
    json input;
    check(fetchInterface(cache, 1430, bar), "1430 readable");
    check(fetchInterface(cache, 105, input), "105 readable");
    const json *regen = bar.is_null() ? nullptr : component(bar, 13);
    check(regen && (*regen)["sprite"]["spriteId"] == 18851, "1430:13 sprite 18851");
    check(regen && regen->at("ops") == json::array({"Regenerate", "Cure Poison"}), "1430:13 ops");
    const json *field = input.is_null() ? nullptr : component(input, 225);
    check(field && (*field)["typeName"] == "input" && (*field)["agentType"] == 12, "105:225 input, agent type 12");
    check(field && (*field)["input"]["hover"]["leftX"] == 16, "105:225 input hover record aligned");
}

void expectLevel(nxt_cache *cache, int stat, uint32_t xp, int level)
{
    int32_t got = -1;
    nxt_result rc = nxt_stat_level_for_xp(cache, stat, xp, &got);
    check(rc == NXT_OK && got == level, "stat " + std::to_string(stat) + " xp " + std::to_string(xp) +
                                            " -> level " + std::to_string(got) + " (want " +
                                            std::to_string(level) + ")");
}

void testStats(nxt_cache *cache)
{
    std::printf("stat definitions (defaults 28/9)\n");
    nxt_stat_info attack{};
    attack.struct_size = sizeof(nxt_stat_info);
    check(nxt_get_stat_info(cache, 0, &attack) == NXT_OK, "stat 0 info");
    check(attack.version == NXT_STAT_INFO_VERSION && attack.base_level == 1, "stat 0 base level 1");
    check(attack.xp_table_index == -1 && attack.xp_table_length == 120, "stat 0 uses the 120-entry default table");

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

    nxt_stat_info herblore{};
    herblore.struct_size = sizeof(nxt_stat_info);
    check(nxt_get_stat_info(cache, 15, &herblore) == NXT_OK, "stat 15 info");
    check(herblore.cap_level == 5 && herblore.cap_xp_tenths == 3880, "stat 15 cap level 5, cap xp 388.0");

    nxt_stat_info invention{};
    invention.struct_size = sizeof(nxt_stat_info);
    check(nxt_get_stat_info(cache, 26, &invention) == NXT_OK, "stat 26 info");
    check(invention.xp_table_index == 0 && invention.xp_table_length == 150 && invention.base_level == 0,
          "stat 26 uses table 0 (150 entries), base level 0");
    expectLevel(cache, 26, 829, 1);
    expectLevel(cache, 26, 830, 2);

    nxt_stat_info missing{};
    missing.struct_size = sizeof(nxt_stat_info);
    check(nxt_get_stat_info(cache, 200, &missing) == NXT_ERR_NOT_FOUND, "stat 200 -> NXT_ERR_NOT_FOUND");
    missing.struct_size = 4;
    check(nxt_get_stat_info(cache, 0, &missing) == NXT_ERR_INVALID, "short struct_size -> NXT_ERR_INVALID");

    char *text = nullptr;
    size_t len = 0;
    check(nxt_get_stats_json(cache, &text, &len) == NXT_OK, "stats json");
    if (text)
    {
        json doc = json::parse(std::string(text, len));
        check(doc["stats"].size() >= 29, "stats json lists >= 29 stats");
        nxt_free(text);
    }
}

}  // namespace

int main(int argc, char **argv)
{
    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(path))
    {
        std::printf("SKIPPED: cache directory %s not found\n", path.c_str());
        return kExitSkip;
    }
    nxt_cache *cache = nxt_cache_open_local(path.c_str());
    if (!cache || nxt_cache_enable_live_fallback(cache) != NXT_OK)
    {
        std::printf("SKIPPED: cache %s unusable or live JS5 unreachable: %s\n", path.c_str(), nxt_last_error());
        if (cache)
        {
            nxt_cache_close(cache);
        }
        return kExitSkip;
    }
    testStats(cache);
    testDialog(cache);
    testBank(cache);
    testActionBarAndInput(cache);
    testFullArchive(cache);
    nxt_cache_close(cache);
    std::printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
