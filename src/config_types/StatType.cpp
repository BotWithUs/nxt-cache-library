// Stat definitions decoder; see StatType.h for the client functions mirrored.

#include "config_types/StatType.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{

constexpr int kDefaultTableLength = 120;
constexpr int kTableListEnd = 0xFF;

void need(const RSBuffer &buffer, size_t n)
{
    if (!buffer.canRead(n))
    {
        throw std::runtime_error("stat defaults truncated at offset " +
                                 std::to_string(buffer.readPosition));
    }
}

int u8(RSBuffer &buffer)
{
    need(buffer, 1);
    return buffer.readUnsignedByte();
}

// sub_3D59A0, opcode 1: one stat record.
StatType readStat(RSBuffer &buffer, const StatDefaults &defs)
{
    StatType s;
    s.id = u8(buffer);
    need(buffer, 2);
    s.maxLevel = buffer.readUnsignedShort();
    s.flags = u8(buffer);
    if (s.flags & 2)
    {
        s.capLevelRaw = u8(buffer);
    }
    if (s.flags & 4)
    {
        s.xpTableIndex = u8(buffer);
    }
    if (s.flags & 8)
    {
        s.baseLevel = u8(buffer);
    }
    s.trailingFlag = u8(buffer) == 1;
    if (s.flags & 1)
    {
        s.capLevel = std::min(s.capLevelRaw, s.maxLevel);
        int span = s.capLevel - s.baseLevel;
        s.capXpTenths = 0;
        const auto &table = defs.xpTableFor(s);
        if (span > 0 && !table.empty())
        {
            size_t index = std::min(static_cast<size_t>(span), table.size()) - 1;
            s.capXpTenths = static_cast<int>(10 * table[index]);
        }
    }
    return s;
}

// sub_3D59A0, opcode 2: XP tables, terminated by index 0xFF.
void readTables(RSBuffer &buffer, StatDefaults &defs)
{
    int count = u8(buffer);
    defs.xpTables.assign(static_cast<size_t>(count), defaultStatXpTable());
    for (int index = u8(buffer); index != kTableListEnd; index = u8(buffer))
    {
        if (index >= count)
        {
            throw std::runtime_error("stat xp table index " + std::to_string(index) +
                                     " out of range " + std::to_string(count));
        }
        need(buffer, 2);
        int n = buffer.readUnsignedShort();
        need(buffer, static_cast<size_t>(n) * 4);
        std::vector<uint32_t> table(static_cast<size_t>(n));
        for (auto &xp : table)
        {
            xp = static_cast<uint32_t>(buffer.readInt());
        }
        defs.xpTables[static_cast<size_t>(index)] = std::move(table);
    }
}

}  // namespace

const std::vector<uint32_t> &defaultStatXpTable()
{
    static const std::vector<uint32_t> kTable = []
    {
        std::vector<uint32_t> table;
        uint32_t points = 0;
        for (int level = 1; level <= kDefaultTableLength; level++)
        {
            points += static_cast<uint32_t>(std::floor(std::pow(2.0, level / 7.0) * 300.0 + level));
            table.push_back(points >> 2);
        }
        return table;
    }();
    return kTable;
}

void StatDefaults::decode(RSBuffer &buffer)
{
    stats.clear();
    xpTables.clear();
    for (int op = u8(buffer); op != 0; op = u8(buffer))
    {
        if (op == 1)
        {
            int count = u8(buffer);
            for (int i = 0; i < count; i++)
            {
                stats.push_back(readStat(buffer, *this));
            }
        }
        else if (op == 2)
        {
            readTables(buffer, *this);
        }
        // Any other opcode carries no payload: the client just reads the next.
    }
}

const StatType *StatDefaults::find(int statId) const
{
    for (const auto &s : stats)
    {
        if (s.id == statId)
        {
            return &s;
        }
    }
    return nullptr;
}

const std::vector<uint32_t> &StatDefaults::xpTableFor(const StatType &stat) const
{
    if (stat.xpTableIndex >= 0 && static_cast<size_t>(stat.xpTableIndex) < xpTables.size())
    {
        return xpTables[static_cast<size_t>(stat.xpTableIndex)];
    }
    return defaultStatXpTable();
}

int StatDefaults::levelForXp(const StatType &stat, uint32_t xp) const
{
    const auto &table = xpTableFor(stat);
    int passed = 0;
    while (static_cast<size_t>(passed) < table.size() && xp >= table[static_cast<size_t>(passed)])
    {
        passed++;
    }
    return std::min(stat.baseLevel + passed, stat.maxLevel);
}
