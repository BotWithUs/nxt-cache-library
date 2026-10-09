#pragma once

#include "core/RSBuffer.h"

#include <cstdint>
#include <string>
#include <vector>

// Stat (skill) definitions: JS5 index 28 (defaults), group 9, file 0.
//
// Decoded exactly as the 950-1 rs2client.exe does it (image-base-relative EAs):
//   sub_2AF0D0  the defaults-group dispatcher; group 9 allocates the 48-byte
//               stats object and calls
//   sub_3D59A0  the stats decoder (opcode loop below)
//   sub_3D5880  the default XP table (120 entries, the classic formula), used
//               by every stat that names no table
//   jag::game::StatManager::SetStat (0x3573F0)  turns XP into a level
//
// Top-level opcodes until 0:
//   1: u8 count, then per stat: u8 id, u16 maxLevel, u8 flags,
//      [flags & 2] u8 capLevel, [flags & 4] u8 xpTableIndex,
//      [flags & 8] u8 baseLevel (default 1), u8 trailing flag (== 1).
//   2: u8 tableCount, then (u8 index, u16 n, n x u32 xp) until index 0xFF.
struct StatType
{
    int id{-1};
    int maxLevel{};
    int flags{};
    int capLevelRaw{};      // the flags & 2 byte (0 when absent)
    int capLevel{-1};       // flags & 1: min(capLevelRaw, maxLevel); else -1
    int capXpTenths{-1};    // flags & 1: 10 x XP at capLevel (client +12); else -1
    int xpTableIndex{-1};   // flags & 4; -1 = the default computed table
    int baseLevel{1};       // flags & 8
    bool trailingFlag{};    // the record's last byte == 1 (meaning not established)
};

struct StatDefaults
{
    std::vector<StatType> stats;                 // in file order
    std::vector<std::vector<uint32_t>> xpTables; // opcode 2, by index

    // Throws std::runtime_error on a truncated or malformed group.
    void decode(RSBuffer &buffer);

    [[nodiscard]] const StatType *find(int statId) const;

    // The table a stat levels against: its opcode-2 table, or the default.
    [[nodiscard]] const std::vector<uint32_t> &xpTableFor(const StatType &stat) const;

    // StatManager::SetStat for whole-XP values: baseLevel plus the number of
    // table entries <= xp, capped at maxLevel.
    [[nodiscard]] int levelForXp(const StatType &stat, uint32_t xp) const;
};

// sub_3D5880: entry i is the XP for level i + 2 (83, 174, 276, ...).
const std::vector<uint32_t> &defaultStatXpTable();
