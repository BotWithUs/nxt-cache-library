#include "config_types/StyleSheetType.h"

#include <set>
#include <utility>

namespace {

// The registered (decoded) sheet with this id, or nullptr.
const StyleSheetType *findRegistered(const std::map<int, StyleSheetType> &sheets, int id)
{
    const auto it = sheets.find(id);
    if (it == sheets.end() || !it->second.isDecodeOk)
    {
        return nullptr;
    }
    return &it->second;
}

}  // namespace

void StyleSheetType::decode(RSBuffer &buffer)
{
    // Decode into locals and publish only on success, so a failed sheet never
    // carries a partial entry list.
    parentId = -1;
    entries.clear();
    isDecodeOk = false;
    badValueType = -1;
    trailingBytes = 0;

    const int parent = static_cast<int16_t>(buffer.readUnsignedShort());
    const int count = buffer.readUnsignedShort();
    if (buffer.hasOverrun)
    {
        return;
    }
    std::vector<StyleSheetEntry> decoded;
    decoded.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++)
    {
        const int valueType = buffer.readUnsignedByte();
        StyleSheetEntry entry;
        entry.keyHash = buffer.readInt();
        entry.value = buffer.readInt();
        if (buffer.hasOverrun)
        {
            return;
        }
        if (valueType != 0)
        {
            badValueType = valueType;
            return;
        }
        decoded.push_back(entry);
    }
    parentId = parent;
    entries = std::move(decoded);
    trailingBytes = buffer.remaining();
    isDecodeOk = true;
}

std::optional<int32_t> StyleSheetType::find(int32_t keyHash) const
{
    for (const StyleSheetEntry &entry : entries)
    {
        if (entry.keyHash == keyHash)
        {
            return entry.value;
        }
    }
    return std::nullopt;
}

std::optional<StyleSheetResolvedValue> resolveStyleSheetValue(
    const std::map<int, StyleSheetType> &sheets, int id, int32_t keyHash)
{
    std::set<int> visited;
    for (const StyleSheetType *sheet = findRegistered(sheets, id);
         sheet != nullptr && visited.insert(sheet->id).second;
         sheet = findRegistered(sheets, sheet->parentId))
    {
        if (const auto value = sheet->find(keyHash))
        {
            return StyleSheetResolvedValue{*value, sheet->id};
        }
    }
    return std::nullopt;
}

std::map<int32_t, StyleSheetResolvedValue> resolveStyleSheet(
    const std::map<int, StyleSheetType> &sheets, int id)
{
    std::map<int32_t, StyleSheetResolvedValue> resolved;
    std::set<int> visited;
    for (const StyleSheetType *sheet = findRegistered(sheets, id);
         sheet != nullptr && visited.insert(sheet->id).second;
         sheet = findRegistered(sheets, sheet->parentId))
    {
        // emplace keeps the first value seen: the sheet's own first occurrence
        // beats its later duplicates, and a child beats its parents.
        for (const StyleSheetEntry &entry : sheet->entries)
        {
            resolved.emplace(entry.keyHash, StyleSheetResolvedValue{entry.value, sheet->id});
        }
    }
    return resolved;
}
