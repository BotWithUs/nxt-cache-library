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
// Verified byte-for-byte against the beta cache (build 947).
inline const std::vector<std::pair<int, std::string>> &gameValGroups()
{
    static const std::vector<std::pair<int, std::string>> kGroups = {
        {0, "component"}, {5, "bas"}, {9, "category"}, {12, "cursor"},
        {14, "dbrow"}, {15, "dbtable"}, {16, "enum"}, {20, "headbar"},
        {21, "hitmark"}, {24, "interface"}, {25, "inv"}, {28, "loc"},
        {32, "material"}, {34, "model"}, {35, "npc"}, {36, "obj"},
        {37, "param"}, {41, "quest"}, {44, "seq"}, {49, "graphic"},
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

}  // namespace nxt
