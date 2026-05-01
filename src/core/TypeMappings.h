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
        {"varbit",   {"varbit",   22, -1, 10}},
        {"enum",     {"enum",     17, -1,  8}},
        {"struct",   {"struct",   26, -1, 10}},
        {"inv",      {"inv",       2,  5,  0}},
        {"param",    {"param",     2, 11,  0}},
        {"quest",    {"quest",     2, 35,  0}},
        {"underlay", {"underlay",  2,  1,  0}},
        {"overlay",  {"overlay",   2,  4,  0}},
        {"worldmap", {"worldmap", 23,  0,  0}},
        {"dbrow",    {"dbrow",     2, 41,  0}},
    };
    return kDefaults;
}

}  // namespace nxt
