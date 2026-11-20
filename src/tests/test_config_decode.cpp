// Decodes every npc, loc and quest config in a real cache and requires each one to end
// exactly on its terminating opcode 0 with no unknown opcode. A decoder that reads
// the wrong width for an opcode drifts out of alignment, and the drift shows up here
// as an early stop or an unknown opcode instead of as silently wrong fields.
//
//   nxtcache-config-decode-test [cache-dir]
//
// The synthetic quest records run first and need no cache.
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP).

#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/RSCache.h"
// RSCache.h only forward-declares these, but its inline constructor needs them
// complete to destroy its unique_ptr members.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kNpcIndex = 18;
constexpr int kNpcShift = 7;
constexpr int kLocIndex = 16;
constexpr int kLocShift = 8;
constexpr int kConfigIndex = 2;
constexpr int kQuestArchive = 35;
constexpr int kMaxReported = 20;
constexpr int kHansId = 0;

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

// Empty string when the config decoded cleanly, otherwise why it did not.
template<typename T>
std::string decodeProblem(RSBuffer &buffer, T &type)
{
    type.decode(buffer);
    if (type.unknownOpcode != -1)
    {
        return "unknown opcode " + std::to_string(type.unknownOpcode);
    }
    const bool isTerminated = buffer.readPosition > 0 && buffer.buffer[buffer.readPosition - 1] == 0;
    if (buffer.remaining() != 0 || !isTerminated)
    {
        return "stopped at " + std::to_string(buffer.readPosition) + "/" +
               std::to_string(buffer.writePosition);
    }
    return {};
}

// With no archiveIds, sweeps every archive of the index with ids sharded by shift;
// otherwise only those archives, with the file id as the config id.
template<typename T>
void sweepAll(RSCache &cache, const char *name, int index, int shift, std::vector<int> archiveIds = {})
{
    int decoded = 0;
    int failed = 0;
    const bool isSharded = archiveIds.empty();
    if (isSharded)
    {
        archiveIds = cache.archiveIds(index);
    }
    for (int archiveId : archiveIds)
    {
        Archive &archive = cache.archive(index, archiveId);
        if (archive.id == -1)
        {
            continue;
        }
        for (auto &[fileId, unused] : archive.files)
        {
            RSBuffer buffer = archive.readFile(fileId);
            if (buffer.buffer == nullptr || buffer.remaining() == 0)
            {
                continue;
            }
            T type;
            type.id = isSharded ? (archiveId << shift) | fileId : fileId;
            const std::string problem = decodeProblem(buffer, type);
            if (problem.empty())
            {
                decoded++;
                continue;
            }
            if (failed++ < kMaxReported)
            {
                std::printf("    %s %d: %s\n", name, type.id, problem.c_str());
            }
        }
    }
    std::printf("  %s: decoded %d, failed %d\n", name, decoded, failed);
    check(decoded > 0, std::string("at least one ") + name + " decoded");
    check(failed == 0, std::string("every ") + name + " ends on opcode 0 with no unknown opcode");
}

// Hans carries opcode 27 ahead of his name and options, so a decoder without it
// returns him nameless.
void checkHans(RSCache &cache)
{
    Archive &archive = cache.archive(kNpcIndex, kHansId >> kNpcShift);
    RSBuffer buffer = archive.readFile(kHansId & ((1 << kNpcShift) - 1));
    NpcType hans;
    hans.decode(buffer);
    check(hans.name == "Hans", "npc 0 name is Hans (got \"" + hans.name + "\")");
    check(hans.options[0] == "Talk-to" || hans.options[0] == "Talk to",
          "npc 0 first option is Talk to (got \"" + hans.options[0] + "\")");
}

std::string formatVars(const std::vector<std::array<int, 3>> &vars)
{
    std::string out;
    for (const auto &v : vars)
    {
        out += "{" + std::to_string(v[0]) + "," + std::to_string(v[1]) + "," + std::to_string(v[2]) + "}";
    }
    return out;
}

QuestType decodeBytes(std::vector<unsigned char> bytes, unsigned int &outReadPosition)
{
    RSBuffer buffer(reinterpret_cast<char *>(bytes.data()), bytes.size());
    QuestType quest;
    quest.decode(buffer);
    outReadPosition = buffer.readPosition;
    return quest;
}

// Hand-built records in the 951 layout, so the opcode widths are pinned without a cache.
void checkSyntheticQuests()
{
    std::printf("synthetic quest records\n");
    // 2 listName "A"; 22 one 24-bit varbit {9803, 2, 18}; 1 name "B"; 0.
    const std::vector<unsigned char> progress = {
        0x02, 0x00, 'A', 0x00,
        0x16, 0x01, 0x00, 0x26, 0x4B, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x12,
        0x01, 0x00, 'B', 0x00,
        0x00};
    unsigned int position = 0;
    QuestType quest = decodeBytes(progress, position);
    check(quest.unknownOpcode == -1 && position == progress.size(), "opcode 22 record consumed exactly");
    check(quest.name == "B", "name after opcode 22 is B (got \"" + quest.name + "\")");
    check(formatVars(quest.progressVarbits) == "{9803,2,18}",
          "opcode 22 is a 24-bit varbit progress var (got " + formatVars(quest.progressVarbits) + ")");

    // 18 one varp requirement {300, 1..5, "R"}; 19 one varbit requirement {70000, 0..2, ""}; 0.
    const std::vector<unsigned char> requirements = {
        0x12, 0x01, 0x00, 0x00, 0x01, 0x2C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 'R', 0x00,
        0x13, 0x01, 0x00, 0x01, 0x11, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00};
    quest = decodeBytes(requirements, position);
    check(position == requirements.size(), "opcode 18/19 record consumed exactly");
    const bool isVarpOk = quest.varpRequirements.size() == 1 && quest.varpRequirements[0].id == 300 &&
                          quest.varpRequirements[0].min == 1 && quest.varpRequirements[0].max == 5 &&
                          quest.varpRequirements[0].description == "R";
    const bool isVarbitOk = quest.varbitRequirements.size() == 1 && quest.varbitRequirements[0].id == 70000 &&
                            quest.varbitRequirements[0].min == 0 && quest.varbitRequirements[0].max == 2;
    check(isVarpOk, "opcode 18 decodes {varp 300, 1..5, \"R\"}");
    check(isVarbitOk, "opcode 19 decodes {varbit 70000, 0..2}");

    // 200 is not a quest opcode: the decode stops there instead of reading 0x01 as a name.
    const std::vector<unsigned char> unknown = {0x09, 0x02, 0xC8, 0x01, 0x00, 'X', 0x00, 0x00};
    quest = decodeBytes(unknown, position);
    check(quest.unknownOpcode == 200,
          "unknown opcode 200 is recorded (got " + std::to_string(quest.unknownOpcode) + ")");
    check(quest.questPoints == 2 && quest.name.empty(), "decode stops at the unknown opcode");
}

struct QuestExpectation
{
    int id;
    const char *name;
    bool isVarbit;
    std::array<int, 3> var;
};

// Real 951 quests. 51, 111, 137 and 258 decoded to garbage while opcode 22 was unknown;
// 34 and 257 carry a varp and decoded correctly throughout.
void checkNamedQuests(RSCache &cache)
{
    const QuestExpectation expected[] = {
        {51, "Demon Slayer", true, {17841, 1, 9}},
        {111, "Druidic Ritual", true, {13366, 2, 8}},
        {137, "Goblin Diplomacy", true, {9363, 1, 6}},
        {258, "Vampyre Slayer", true, {9655, 1, 3}},
        {34, "Dwarf Cannon", false, {2732, 1, 11}},
        {257, "Cook's Assistant", false, {2492, 1, 2}},
    };
    Archive &archive = cache.archive(kConfigIndex, kQuestArchive);
    for (const QuestExpectation &e : expected)
    {
        RSBuffer buffer = archive.readFile(e.id);
        QuestType quest;
        quest.id = e.id;
        quest.decode(buffer);
        const auto &vars = e.isVarbit ? quest.progressVarbits : quest.progressVarps;
        const std::string want = formatVars({e.var});
        check(quest.name == e.name && formatVars(vars) == want,
              "quest " + std::to_string(e.id) + " is " + e.name + (e.isVarbit ? " varbit " : " varp ") + want +
              " (got \"" + quest.name + "\" " + formatVars(vars) + ")");
    }
}

} // namespace

int main(int argc, char **argv)
{
    checkSyntheticQuests();
    const std::string path = cachePathFrom(argc, argv);
    const std::filesystem::path dir(path);
    const bool hasCache = std::filesystem::exists(dir / "js5-18.jcache") &&
                          std::filesystem::exists(dir / "js5-16.jcache") &&
                          std::filesystem::exists(dir / "js5-2.jcache");
    if (!hasCache)
    {
        std::printf("SKIP: no npc/loc/config cache at %s (%d synthetic failure(s))\n", path.c_str(), failures);
        return failures == 0 ? kExitSkip : 1;
    }
    RSCache cache(path);
    std::printf("config decode against %s\n", path.c_str());
    sweepAll<NpcType>(cache, "npc", kNpcIndex, kNpcShift);
    checkHans(cache);
    sweepAll<LocationType>(cache, "loc", kLocIndex, kLocShift);
    sweepAll<QuestType>(cache, "quest", kConfigIndex, 0, {kQuestArchive});
    checkNamedQuests(cache);
    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
