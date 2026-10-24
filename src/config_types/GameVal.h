#pragma once

#include "core/RSBuffer.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// GameVal (JS5 index 67) — symbolic id<->name tables Jagex publishes for every
// scriptable type (loc, npc, obj, component, var_player, ...). Each index-67
// archive is one type's whole table, stored as a single file (id 0). The table
// is decoded here; the archive-id -> type-name map is fixed (a GameValGroupType
// ordinal, NOT a name hash).
namespace nxt {

constexpr int kGameValIndex = 67;

struct GameValEntry
{
    int id;
    std::string name;  // symbolic name; stored UPPERCASE in the cache
};

// All known gameval groups in ascending archive-id order: {archiveId, name}.
// Verified against the beta cache (build 947) and against both the live 951-1
// cache and the beta 951 JS5 server (2026-10-23): live and beta carry the same
// 36 archive ids, with no id naming a different group on either side. Archive
// 29 (mapelement) is the one the 947 map lacked: its 5843 ids are exactly the
// map element configs (index 2 archive 36), name for name.
inline const std::vector<std::pair<int, std::string>> &gameValGroups()
{
    static const std::vector<std::pair<int, std::string>> kGroups = {
        {0, "component"}, {5, "bas"}, {9, "category"}, {12, "cursor"},
        {14, "dbrow"}, {15, "dbtable"}, {16, "enum"}, {20, "headbar"},
        {21, "hitmark"}, {24, "interface"}, {25, "inv"}, {28, "loc"},
        {29, "mapelement"}, {32, "material"}, {34, "model"}, {35, "npc"},
        {36, "obj"}, {37, "param"}, {41, "quest"}, {44, "seq"}, {49, "graphic"},
        {50, "struct"}, {55, "var_clan"}, {56, "var_clan_setting"},
        {57, "var_client"}, {59, "var_npc"}, {60, "var_object"},
        {61, "var_player"}, {64, "sound"}, {69, "midi"},
        {80, "var_player_group"}, {89, "achievement"}, {90, "fontmetrics"},
        {92, "stylesheet"}, {96, "ui_anim_curve"}, {97, "ui_anim"},
    };
    return kGroups;
}

// Canonical type name for a gameval group archive id, or "" if unknown.
inline std::string gameValGroupName(int archiveId)
{
    for (const auto &[aid, name] : gameValGroups())
    {
        if (aid == archiveId)
        {
            return name;
        }
    }
    return {};
}

// Archive id for a gameval group name, or -1 if unknown.
inline int gameValGroupArchive(const std::string &name)
{
    for (const auto &[aid, gname] : gameValGroups())
    {
        if (gname == name)
        {
            return aid;
        }
    }
    return -1;
}

namespace detail {

// Read the (id, offset) index that precedes the string blob. format 2 stores
// explicit ids (sparse); format 1 is a dense array indexed by id, with an
// offset of -1 marking an unused id.
inline std::vector<std::pair<int, int>> readGameValIndex(RSBuffer &buffer, int format, int count)
{
    std::vector<std::pair<int, int>> entries;
    entries.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++)
    {
        if (format == 2)
        {
            int id = buffer.readInt();
            int off = buffer.readInt();
            entries.emplace_back(id, off);
        }
        else
        {
            entries.emplace_back(i, buffer.readInt());
        }
    }
    return entries;
}

}  // namespace detail

// Decode one gameval table file (an index-67 archive's file 0) into entries
// sorted by id. Returns empty on a too-short or unknown-format buffer.
inline std::vector<GameValEntry> decodeGameVals(RSBuffer &buffer)
{
    std::vector<GameValEntry> out;
    if (buffer.remaining() < 8)
    {
        return out;
    }
    int format = buffer.readInt();
    int count = buffer.readInt();
    if (count < 0 || (format != 1 && format != 2))
    {
        return out;
    }

    std::vector<std::pair<int, int>> entries = detail::readGameValIndex(buffer, format, count);

    // Names live in a trailing NUL-separated blob; offsets index into it. Slice
    // each name as [offset, nextOffsetInOffsetOrder) so we never run off the end
    // of an unterminated final string, then trim at the first NUL.
    const char *blob = buffer.buffer + buffer.readPosition;
    const size_t blobLen = buffer.remaining();

    std::vector<std::pair<int, int>> valid;
    valid.reserve(entries.size());
    for (const auto &e : entries)
    {
        if (e.second >= 0 && static_cast<size_t>(e.second) <= blobLen)
        {
            valid.push_back(e);
        }
    }
    std::sort(valid.begin(), valid.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });

    out.reserve(valid.size());
    for (size_t i = 0; i < valid.size(); i++)
    {
        const size_t start = static_cast<size_t>(valid[i].second);
        size_t end = (i + 1 < valid.size()) ? static_cast<size_t>(valid[i + 1].second) : blobLen;
        end = std::clamp(end, start, blobLen);
        size_t len = 0;
        while (len < (end - start) && blob[start + len] != '\0')
        {
            len++;
        }
        out.push_back(GameValEntry{valid[i].first, std::string(blob + start, len)});
    }

    std::sort(out.begin(), out.end(),
              [](const GameValEntry &a, const GameValEntry &b) { return a.id < b.id; });
    return out;
}

// Entity type a gameval group is published under in gameval.sqlite (the name
// the Java host's GamevalType enum uses). Only two groups are renamed: "obj" is
// the game's word for item, and "var_player" is what the host calls a varp.
inline std::string gameValEtype(const std::string &group)
{
    if (group == "obj")
    {
        return "item";
    }
    if (group == "var_player")
    {
        return "varp";
    }
    return group;
}

// A var_* archive that packs a second, '_'-prefixed bit table after its vars:
// the bits are varbits of one domain (VarbitType::domainType), named under
// bitEtype. There is no separate index-67 archive for any bit table.
struct GameValBitTable
{
    int varArchive;
    const char *bitEtype;
    int varbitDomain;
};

// Domains measured on live 951-1: each table's bits are exactly the varbit
// configs (index 2 archive 69) of this domainType, and no others.
inline const std::vector<GameValBitTable> &gameValBitTables()
{
    static const std::vector<GameValBitTable> kTables = {
        {55, "varbit_clan", 6},
        {56, "varbit_clan_setting", 7},
        {59, "varbit_npc", 1},
        {60, "varbit_object", 5},
        {61, "varbit", 0},
    };
    return kTables;
}

// The bit table packed in a var_* archive, or nullptr if it has none.
inline const GameValBitTable *gameValBitTable(int archiveId)
{
    for (const auto &t : gameValBitTables())
    {
        if (t.varArchive == archiveId)
        {
            return &t;
        }
    }
    return nullptr;
}

struct GameValSplit
{
    std::vector<GameValEntry> vars;
    std::vector<GameValEntry> bits;  // re-based to varbit ids, '_' stripped
    int bitBase = 0;                 // raw id of varbit 0 (the var table size)
    bool isSplit = false;
};

// Split a var_* table into its vars and its packed bits. The table is one
// dense array: ids [0, N) are the N vars, and from N on, raw id N + b names
// varbit b with a leading '_'. N is the var table size, i.e. last var id + 1.
// It is NOT the first '_' id: varbit 0..k of other domains are unnamed holes
// (the Python builder rebased on the first '_' id and shifted four of the five
// bit tables by hundreds to thousands of ids). The split only applies when
// every '_' id lies above every var id; otherwise isSplit is false and vars is
// the input unchanged, since several plain groups merely contain '_' names.
inline GameValSplit splitGameValBitTable(const std::vector<GameValEntry> &entries)
{
    GameValSplit out;
    int maxPlain = -1;
    int minMarked = -1;
    for (const auto &e : entries)
    {
        const bool isMarked = !e.name.empty() && e.name[0] == '_';
        if (isMarked)
        {
            minMarked = (minMarked == -1) ? e.id : std::min(minMarked, e.id);
        }
        else
        {
            maxPlain = std::max(maxPlain, e.id);
        }
    }
    if (minMarked == -1 || minMarked <= maxPlain)
    {
        out.vars = entries;
        return out;
    }
    out.isSplit = true;
    out.bitBase = maxPlain + 1;
    for (const auto &e : entries)
    {
        if (e.id < out.bitBase)
        {
            out.vars.push_back(e);
        }
        else
        {
            out.bits.push_back(GameValEntry{e.id - out.bitBase, e.name.substr(1)});
        }
    }
    return out;
}

}  // namespace nxt
