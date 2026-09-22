#pragma once

#include <map>
#include <string>

namespace nxt {

// Per-type cache layout: which JS5 index, which archive (or sharded by id), and
// the bit-shift used to split id into (archive, file) for sharded layouts.
// Values match the common NXT/RS3 cache; override per-call when targeting a
// different revision.
struct TypeMapping
{
    std::string name;
    int indexId;
    int archiveId;  // -1 indicates sharded layout
    int shift;
};

inline const std::map<std::string, TypeMapping> &typeDefaults()
{
    static const std::map<std::string, TypeMapping> kDefaults = {
        {"npc",      {"npc",      18, -1,  7}},
        {"item",     {"item",     19, -1,  8}},
        {"loc",      {"loc",      16, -1,  8}},
        {"seq",      {"seq",      20, -1,  7}},
        {"varbit",   {"varbit",    2, 69,  0}},
        {"varp",     {"varp",      2, 60,  0}},   // VAR_PLAYER; see VarPlayerType.h
        {"enum",     {"enum",     17, -1,  8}},
        {"struct",   {"struct",   22, -1,  5}},
        {"inv",      {"inv",       2,  5,  0}},
        {"param",    {"param",     2, 11,  0}},
        {"quest",    {"quest",     2, 35,  0}},
        {"underlay", {"underlay",  2,  1,  0}},
        {"overlay",  {"overlay",   2,  4,  0}},
        {"worldmap", {"worldmap",  2, 36,  0}},
        {"dbrow",    {"dbrow",     2, 41,  0}},
        // Models and sprites: one archive per id, single file (file 0). shift=0
        // makes the generic shard math resolve to archive=id, file=0. The modern
        // RS3 NXT cache remaps assets to high indices: models live at index 47
        // (LZMA-wrapped, JS5 type 3), sprites at index 8. (Textures, out of scope
        // here, are at 52=DDS / 54=raw.) Override --index per-call for other
        // revisions where the model index differs.
        {"model",    {"model",    47, -1,  0}},
        {"sprite",   {"sprite",    8, -1,  0}},
        // Interfaces: cache index 3, each archive = one interface, each file
        // in that archive = one component. There is no (id, archive, file)
        // sharding — callers pass the interface id as the archive id and
        // sweep all files within. We park `archiveId=-1` and `shift=0` as
        // sentinels; the C-ABI interface getter ignores TypeMapping fields
        // and addresses the archive directly.
        {"if",       {"if",        3, -1,  0}},
        // GameVals: cache index 67, one archive per type (archive id = a fixed
        // GameValGroupType ordinal), each archive holds a single id->name table
        // file. archiveId=-1 = sweep all groups; the dumper's gameval path uses
        // a dedicated decoder (config_types/GameVal.h), not the shard loader.
        {"gameval",  {"gameval",  67, -1,  0}},
    };
    return kDefaults;
}

}  // namespace nxt
