#pragma once

#include "core/RSBuffer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

// Stylesheet (UI skin) decoder for JS5 index 60. One group per stylesheet, the
// group id is the stylesheet id, the sheet is file 0 of the group. A component
// selects one through its styleSheetId (InterfaceComponentDef), and dbtable 132
// interface_skin lists them per theme. Format and client behaviour from the
// 951-4 static RE (NXTLibrary claudedocs/plugin-ui-p2-map-951-4.md 15.4,
// StyleSheet__Decode 0xBAD7B0, StyleSheet__FindValue 0xBADA60):
//
//   i16 BE parentId               -1 = none
//   u16 BE count
//   count x { u8 valueType        must be 0; anything else fails the whole sheet
//             i32 BE keyHash      Java String.hashCode of the property name
//             i32 BE value }
//
// Lookup: the first entry with a hash wins inside a sheet (the client skips an
// insert on an equal hash), then the parent chain is walked.

struct StyleSheetEntry
{
    int32_t keyHash{};
    int32_t value{};
};

class StyleSheetType
{
public:
    int id{-1};
    int parentId{-1};
    std::vector<StyleSheetEntry> entries;   // file order, duplicates kept
    // True when the header and every entry were read and every valueType was
    // 0. The client does not register a sheet whose decode fails, so false
    // means "absent" to the client; entries is then empty and parentId -1.
    bool isDecodeOk{};
    int badValueType{-1};                   // first non-zero valueType, else -1
    // Bytes left after the last entry. Every 951-4 sheet has 0; whether the
    // client rejects a sheet with a tail is not known, so it does not fail
    // isDecodeOk.
    size_t trailingBytes{};

    void decode(RSBuffer &buffer);

    // This sheet's own value for a key (first occurrence), without parents.
    [[nodiscard]] std::optional<int32_t> find(int32_t keyHash) const;
};

// Java String.hashCode over the name's bytes as signed chars, the hash the
// cache stores (the client hashes cp1252 bytes; every known name is ASCII).
[[nodiscard]] constexpr int32_t styleSheetKeyHash(std::string_view name)
{
    uint32_t hash = 0;
    for (const char c : name)
    {
        hash = hash * 31u + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c)));
    }
    return static_cast<int32_t>(hash);
}

// The property name for a key hash, or nullptr when it is not a known name.
// See StyleSheetKeys.cpp for where the names come from.
[[nodiscard]] const char *styleSheetKeyName(int32_t keyHash);
[[nodiscard]] size_t styleSheetKnownKeyCount();

struct StyleSheetResolvedValue
{
    int32_t value{};
    int sourceId{-1};   // the sheet in the chain that supplied the value
};

// The value of a key for sheet `id`, falling back through the parent chain the
// way StyleSheet__FindValue does. Sheets that failed to decode count as absent
// (the client never registers them), as does a missing parent. A parent cycle
// stops the walk instead of looping.
[[nodiscard]] std::optional<StyleSheetResolvedValue> resolveStyleSheetValue(
    const std::map<int, StyleSheetType> &sheets, int id, int32_t keyHash);

// Every key visible from sheet `id` after the parent chain, keyed by hash.
// Same absence and cycle rules as resolveStyleSheetValue.
[[nodiscard]] std::map<int32_t, StyleSheetResolvedValue> resolveStyleSheet(
    const std::map<int, StyleSheetType> &sheets, int id);
