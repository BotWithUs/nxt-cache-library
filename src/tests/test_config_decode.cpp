// Decodes every record of every config type in config_types/Types.h (npc, loc, quest,
// item, enum, struct, inv, param, seq, varbit, underlay, overlay, worldmap, dbrow) in a
// real cache and requires each one to end exactly on its terminating opcode 0, with no
// unknown opcode and no read past the record. A decoder that reads the wrong width for
// an opcode drifts out of alignment, and the drift shows up here as an early stop or an
// unknown opcode instead of as silently wrong fields. The per-type table it prints
// (exact / unknown / overrun / no terminator / trailing bytes) is the audit sweep.
//
//   nxtcache-config-decode-test [cache-dir]
//
// The synthetic records run first and need no cache: quest opcodes, an unknown opcode
// for every decoder, and the opcodes pinned from the 951 cache.
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP).

#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/RSCache.h"
#include "core/TypeMappings.h"
// RSCache.h only forward-declares these, but its inline constructor needs them
// complete to destroy its unique_ptr members.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kNpcIndex = 18;
constexpr int kNpcShift = 7;
constexpr int kConfigIndex = 2;
constexpr int kQuestArchive = 35;
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

// How one record's decode ended. The order is the precedence when several apply.
enum class DecodeEnd
{
    Exact,        // ended on opcode 0 with every byte consumed
    Unknown,      // stopped at (or, in a lenient decoder, skipped) an opcode it does not handle
    Overrun,      // a read wanted more bytes than the record holds
    Unterminated, // consumed every byte without reading the terminating opcode 0
    Trailing,     // read opcode 0 with bytes left over
};

template<typename T>
DecodeEnd classifyDecode(RSBuffer &buffer, T &type)
{
    type.decode(buffer);
    if (type.unknownOpcode != -1)
    {
        return DecodeEnd::Unknown;
    }
    if (buffer.hasOverrun)
    {
        return DecodeEnd::Overrun;
    }
    const bool isTerminated = buffer.readPosition > 0 && buffer.buffer[buffer.readPosition - 1] == 0;
    if (!isTerminated)
    {
        return DecodeEnd::Unterminated;
    }
    return buffer.remaining() == 0 ? DecodeEnd::Exact : DecodeEnd::Trailing;
}

struct SweepStats
{
    int total{};
    int exact{};
    int overrun{};
    int unterminated{};
    int trailing{};
    std::map<int, int> unknownByOpcode;     // first unknown opcode -> records
    std::map<int, std::vector<int>> unknownIds;

    [[nodiscard]] int unknown() const
    {
        int sum = 0;
        for (const auto &[opcode, count] : unknownByOpcode)
        {
            sum += count;
        }
        return sum;
    }
};

template<typename T>
void tally(SweepStats &stats, RSBuffer &buffer, T &type)
{
    stats.total++;
    switch (classifyDecode(buffer, type))
    {
        case DecodeEnd::Exact:
            stats.exact++;
            break;
        case DecodeEnd::Unknown:
            stats.unknownByOpcode[type.unknownOpcode]++;
            stats.unknownIds[type.unknownOpcode].push_back(type.id);
            break;
        case DecodeEnd::Overrun:
            stats.overrun++;
            break;
        case DecodeEnd::Unterminated:
            stats.unterminated++;
            break;
        case DecodeEnd::Trailing:
            stats.trailing++;
            break;
    }
}

// Decodes every record of a type, located by its nxt::typeDefaults() mapping: a fixed
// archive (file id = config id) or every archive of the index sharded by shift.
template<typename T>
SweepStats sweepType(RSCache &cache, const char *name)
{
    const nxt::TypeMapping &mapping = nxt::typeDefaults().at(name);
    SweepStats stats;
    const bool isSharded = mapping.archiveId == -1;
    const std::vector<int> archiveIds = isSharded ? cache.archiveIds(mapping.indexId)
                                                  : std::vector<int>{mapping.archiveId};
    for (int archiveId : archiveIds)
    {
        Archive &archive = cache.archive(mapping.indexId, archiveId);
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
            type.id = isSharded ? (archiveId << mapping.shift) | fileId : fileId;
            tally(stats, buffer, type);
        }
    }
    return stats;
}

std::string formatUnknowns(const SweepStats &stats)
{
    std::string out;
    for (const auto &[opcode, count] : stats.unknownByOpcode)
    {
        out += (out.empty() ? "" : " ") + std::string("op") + std::to_string(opcode) + "x" + std::to_string(count);
        const std::vector<int> &ids = stats.unknownIds.at(opcode);
        out += "(e.g.";
        for (size_t i = 0; i < ids.size() && i < 3; i++)
        {
            out += " " + std::to_string(ids[i]);
        }
        out += ")";
    }
    return out.empty() ? "-" : out;
}

void printSweepRow(const char *name, const SweepStats &stats)
{
    std::printf("  | %-8s | %7d | %7d | %7d | %7d | %7d | %7d | %s\n", name, stats.total, stats.exact,
                stats.unknown(), stats.overrun, stats.unterminated, stats.trailing, formatUnknowns(stats).c_str());
}

// Sweeps one type, prints its table row, and requires every record to decode exactly.
template<typename T>
void sweepAll(RSCache &cache, const char *name)
{
    const SweepStats stats = sweepType<T>(cache, name);
    printSweepRow(name, stats);
    check(stats.total > 0, std::string("at least one ") + name + " decoded");
    check(stats.exact == stats.total,
          std::string("every ") + name + " ends on opcode 0 with every byte consumed and no unknown opcode");
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

template<typename T = QuestType>
T decodeBytes(std::vector<unsigned char> bytes, unsigned int &outReadPosition)
{
    RSBuffer buffer(reinterpret_cast<char *>(bytes.data()), bytes.size());
    T type;
    type.decode(buffer);
    outReadPosition = buffer.readPosition;
    return type;
}

constexpr unsigned char kUnknownOpcode = 200;

// Opcode 200 means nothing to any of these decoders. Each record puts it ahead of a
// field-setting opcode: a decoder that skips the unknown opcode as zero-width (all of
// them before this change) sets that field, one that stops leaves it at its default.
template<typename T, typename Untouched>
void checkStopsAtUnknown(const char *name, std::vector<unsigned char> tail, Untouched isUntouched)
{
    tail.insert(tail.begin(), kUnknownOpcode);
    unsigned int position = 0;
    const T type = decodeBytes<T>(tail, position);
    check(type.unknownOpcode == kUnknownOpcode && position == 1 && isUntouched(type),
          std::string(name) + " stops at unknown opcode 200 and decodes nothing after it (got opcode " +
              std::to_string(type.unknownOpcode) + ", position " + std::to_string(position) + ")");
}

void checkSyntheticUnknownOpcodes()
{
    std::printf("synthetic unknown-opcode records\n");
    checkStopsAtUnknown<VarbitType>("varbit", {0x01, 0x05, 0x00, 0x07, 0x00},
                                    [](const VarbitType &t) { return t.varId == 0 && t.domainType == 0; });
    checkStopsAtUnknown<ItemType>("item", {0x02, 'X', 0x00, 0x00},
                                  [](const ItemType &t) { return t.name.empty(); });
    checkStopsAtUnknown<ParamType>("param", {0x02, 0x00, 0x00, 0x00, 0x05, 0x00},
                                   [](const ParamType &t) { return t.defaultInt == 0; });
    checkStopsAtUnknown<InventoryType>("inv", {0x02, 0x00, 0x10, 0x00},
                                       [](const InventoryType &t) { return t.capacity == 0; });
    checkStopsAtUnknown<EnumType>("enum", {0x04, 0x00, 0x00, 0x00, 0x09, 0x00},
                                  [](const EnumType &t) { return t.intDefault == 0; });
    checkStopsAtUnknown<StructType>("struct", {0xF9, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00},
                                    [](const StructType &t) { return t.params.empty(); });
    checkStopsAtUnknown<SequenceType>("seq", {0x02, 0x00, 0x03, 0x00},
                                      [](const SequenceType &t) { return t.loopOffset == -1; });
    checkStopsAtUnknown<UnderlayType>("underlay", {0x01, 0x12, 0x34, 0x56, 0x00},
                                      [](const UnderlayType &t) { return t.color == 0; });
    checkStopsAtUnknown<OverlayType>("overlay", {0x01, 0x12, 0x34, 0x56, 0x00},
                                     [](const OverlayType &t) { return t.color == 0; });
    checkStopsAtUnknown<WorldMapElementType>("worldmap", {0x13, 0x00, 0x05, 0x00},
                                             [](const WorldMapElementType &t) { return t.elementId == -1; });
    checkStopsAtUnknown<DbRowType>("dbrow", {0x04, 0x05, 0x00},
                                   [](const DbRowType &t) { return t.tableId == -1; });
}

// The opcodes pinned from the 951 cache, in hand-built records, so their widths hold
// without a cache: each record must be consumed exactly and decode the field after it.
void checkSyntheticPinnedOpcodes()
{
    std::printf("synthetic pinned-opcode records\n");
    unsigned int position = 0;

    // As varbit 59651: 1 {domain 0, varp 0x3063}; 2 {lsb 0, msb 4}; 16; 0.
    const std::vector<unsigned char> varbitBytes = {0x01, 0x00, 0x30, 0x63, 0x02, 0x00, 0x04, 0x10, 0x00};
    const VarbitType varbit = decodeBytes<VarbitType>(varbitBytes, position);
    check(varbit.unknownOpcode == -1 && position == varbitBytes.size() && varbit.hasOp16 &&
              varbit.varId == 0x3063 && varbit.msb == 4,
          "varbit opcode 16 is a payload-less flag");

    // As inv 90: 2 {capacity 28}; 24; 0.
    const std::vector<unsigned char> invBytes = {0x02, 0x00, 0x1C, 0x18, 0x00};
    const InventoryType inv = decodeBytes<InventoryType>(invBytes, position);
    check(inv.unknownOpcode == -1 && position == invBytes.size() && inv.hasOp24 && inv.capacity == 28,
          "inv opcode 24 is a payload-less flag");

    // As seq 18025 without its leading opcode 24: 27 {0xFE}; 25 {0x0A97}; 26 {30}; 0.
    const std::vector<unsigned char> seqBytes = {0x1B, 0xFE, 0x19, 0x0A, 0x97, 0x1A, 0x00, 0x00, 0x00, 0x1E, 0x00};
    const SequenceType seq = decodeBytes<SequenceType>(seqBytes, position);
    check(seq.unknownOpcode == -1 && position == seqBytes.size() && seq.op27 == 0xFE && seq.newFramesId == 0x0A97,
          "seq opcode 27 carries one byte (got op27 " + std::to_string(seq.op27) + ")");

    // 250 {varbit 3260, no varp, 246..246}; 251 {no varbit, varp 2205, 1..5};
    // 252 {varbit 22874, no varp, count 1, ids 1992 1993}; 19 {elementId 948}; 0.
    const std::vector<unsigned char> mapBytes = {
        0xFA, 0x00, 0x0C, 0xBC, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xF6, 0x00, 0x00, 0x00, 0xF6,
        0xFB, 0xFF, 0xFF, 0xFF, 0x08, 0x9D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
        0xFC, 0x00, 0x59, 0x5A, 0xFF, 0xFF, 0x01, 0x07, 0xC8, 0x07, 0xC9,
        0x13, 0x03, 0xB4,
        0x00};
    const WorldMapElementType element = decodeBytes<WorldMapElementType>(mapBytes, position);
    check(element.unknownOpcode == -1 && position == mapBytes.size() && element.elementId == 948,
          "worldmap opcodes 250/251/252 consumed exactly");
    check(element.op250 && element.op250->varbitId == 3260 && element.op250->varpId == -1 &&
              element.op250->min == 246 && element.op250->max == 246,
          "worldmap opcode 250 is {24-bit varbit, varp, g4 min, g4 max}");
    check(element.op251 && element.op251->varbitId == -1 && element.op251->varpId == 2205 &&
              element.op251->min == 1 && element.op251->max == 5,
          "worldmap opcode 251 is {24-bit varbit, varp, g4 min, g4 max}");
    check(element.op252 && element.op252->varbitId == 22874 && element.op252->varpId == -1 &&
              element.op252->elementIds == std::vector<int>{1992, 1993},
          "worldmap opcode 252 is {24-bit varbit, varp, count, count + 1 element ids}");
    check(element.varbitId == -1 && element.conditionMin == -1,
          "worldmap opcodes 250-252 leave the opcode 9/26 fields alone");

    // 252 with a count of 2 is a layout no record has shown: it stops instead of guessing.
    const std::vector<unsigned char> wideBytes = {
        0xFC, 0x00, 0x59, 0x5A, 0xFF, 0xFF, 0x02, 0x07, 0xC8, 0x07, 0xC9, 0x07, 0xCA, 0x13, 0x03, 0xB4, 0x00};
    const WorldMapElementType wide = decodeBytes<WorldMapElementType>(wideBytes, position);
    check(wide.unknownOpcode == 252 && wide.elementId == -1 && !wide.op252,
          "worldmap opcode 252 with an unobserved count stops loudly (got " + std::to_string(wide.unknownOpcode) +
              ")");
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

// The raw bytes of one record, located by its nxt::typeDefaults() mapping.
std::vector<unsigned char> readRecord(RSCache &cache, const char *name, int id)
{
    const nxt::TypeMapping &mapping = nxt::typeDefaults().at(name);
    const bool isSharded = mapping.archiveId == -1;
    Archive &archive = cache.archive(mapping.indexId, isSharded ? id >> mapping.shift : mapping.archiveId);
    RSBuffer buffer = archive.readFile(isSharded ? id & ((1 << mapping.shift) - 1) : id);
    if (buffer.buffer == nullptr)
    {
        return {};
    }
    return std::vector<unsigned char>(buffer.buffer, buffer.buffer + buffer.writePosition);
}

// Real 951 records carrying each pinned opcode. Before the pins the worldmap ones decoded
// to garbage after the opcode, and the others skipped it as zero-width.
void checkNamedPinnedRecords(RSCache &cache)
{
    unsigned int position = 0;
    const auto element0 = decodeBytes<WorldMapElementType>(readRecord(cache, "worldmap", 0), position);
    check(element0.op252 && element0.op252->varbitId == 22874 &&
              element0.op252->elementIds == std::vector<int>{1992, 1993} && element0.tooltip == "Map",
          "worldmap 0 is a varbit 22874 switch over 1992/1993 with tooltip Map (got \"" + element0.tooltip + "\")");
    const auto element18 = decodeBytes<WorldMapElementType>(readRecord(cache, "worldmap", 18), position);
    check(element18.op250 && element18.op250->varbitId == 3260 && element18.op250->min == 246 &&
              element18.op251 && element18.op251->varbitId == 43337 && element18.op251->max == 5 &&
              element18.coordinates.size() == 4,
          "worldmap 18 carries 250 {3260, 246} and 251 {43337, 5} ahead of its 4-point polygon");
    const auto seq = decodeBytes<SequenceType>(readRecord(cache, "seq", 18025), position);
    check(seq.op27 == 0xFE && seq.newFramesId == 0x0A97, "seq 18025 is op27 0xFE, newFramesId 2711");
    const auto varbit = decodeBytes<VarbitType>(readRecord(cache, "varbit", 59651), position);
    check(varbit.hasOp16 && varbit.varId == 0x3063 && varbit.msb == 4, "varbit 59651 is varp 12387 bits 0-4, op16");
    const auto inv = decodeBytes<InventoryType>(readRecord(cache, "inv", 90), position);
    check(inv.hasOp24 && inv.capacity == 28, "inv 90 has capacity 28 and op24");
}

int main(int argc, char **argv)
{
    checkSyntheticQuests();
    checkSyntheticUnknownOpcodes();
    checkSyntheticPinnedOpcodes();
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
    std::printf("  | type     |   total |   exact | unknown | overrun | no term | trailing| first unknown opcode x records\n");
    sweepAll<NpcType>(cache, "npc");
    sweepAll<LocationType>(cache, "loc");
    sweepAll<QuestType>(cache, "quest");
    sweepAll<ItemType>(cache, "item");
    sweepAll<EnumType>(cache, "enum");
    sweepAll<StructType>(cache, "struct");
    sweepAll<InventoryType>(cache, "inv");
    sweepAll<ParamType>(cache, "param");
    sweepAll<SequenceType>(cache, "seq");
    sweepAll<VarbitType>(cache, "varbit");
    sweepAll<UnderlayType>(cache, "underlay");
    sweepAll<OverlayType>(cache, "overlay");
    sweepAll<WorldMapElementType>(cache, "worldmap");
    sweepAll<DbRowType>(cache, "dbrow");
    checkHans(cache);
    checkNamedQuests(cache);
    checkNamedPinnedRecords(cache);
    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
