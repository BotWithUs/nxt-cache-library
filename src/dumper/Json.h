#pragma once

#include "config_types/Types.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace nxtdump {

using nlohmann::json;

inline json variantToJson(const std::variant<int, std::string> &v)
{
    return std::visit([](auto &&a) -> json { return a; }, v);
}

inline json variantToJson(const DbCellValue &v)
{
    return std::visit([](auto &&a) -> json { return a; }, v);
}

template<typename V>
inline json intMapToJson(const std::map<int, V> &m)
{
    json j = json::object();
    for (const auto &[k, v]: m)
    {
        if constexpr (std::is_same_v<V, std::variant<int, std::string>>)
            j[std::to_string(k)] = variantToJson(v);
        else
            j[std::to_string(k)] = v;
    }
    return j;
}

inline json toJson(const VarbitType &t)
{
    return {
        {"id", t.id},
        {"varId", t.varId},
        {"domainType", t.domainType},
        {"msb", t.msb},
        {"lsb", t.lsb},
    };
}

inline json toJson(const NpcType &t)
{
    return {
        {"id", t.id},
        {"name", t.name},
        {"options", t.options},
        {"quests", t.quests},
        {"transforms", t.transforms},
        {"params", intMapToJson(t.params)},
        {"drawMapDot", t.drawMapDot},
        {"combatLevel", t.combatLevel},
        {"varbitId", t.varbitId},
        {"varpId", t.varpId},
        {"walkAnimationId", t.walkAnimationId},
        {"rotation", t.rotation},
        {"isVisible", t.isVisible},
        {"isClickable", t.isClickable},
        {"animateIdle", t.animateIdle},
        {"rotate180Animation", t.rotate180Animation},
        {"rotate90RightAnimation", t.rotate90RightAnimation},
        {"rotate90LeftAnimation", t.rotate90LeftAnimation},
    };
}

inline json toJson(const LocationType &t)
{
    json cursors = json::array();
    for (int c: t.cursors) cursors.push_back(c);
    return {
        {"id", t.id},
        {"name", t.name},
        {"options", t.options},
        {"transforms", t.transforms},
        {"animations", t.animations},
        {"params", intMapToJson(t.params)},
        {"varbitId", t.varbitId},
        {"varpId", t.varpId},
        {"sizeX", t.sizeX},
        {"sizeY", t.sizeY},
        {"interactType", t.interactType},
        {"solidType", t.solidType},
        {"scaleX", t.scaleX},
        {"scaleY", t.scaleY},
        {"scaleZ", t.scaleZ},
        {"mapSpriteId", t.mapSpriteId},
        {"mapAreaId", t.mapAreaId},
        {"cursors", cursors},
        {"isMembers", t.isMembers},
    };
}

inline json toJson(const ItemType &t)
{
    json groundCursors = json::array();
    for (int c: t.groundCursors) groundCursors.push_back(c);
    json invCursors = json::array();
    for (int c: t.inventoryCursors) invCursors.push_back(c);
    json stacks = json::array();
    for (auto &[id, amt]: t.stackIDs)
    {
        if (id == 0 && amt == 0) continue;
        stacks.push_back({{"id", id}, {"amount", amt}});
    }
    return {
        {"id", t.id},
        {"name", t.name},
        {"effect", t.effect},
        {"modelID", t.modelID},
        {"modelZoom", t.modelZoom},
        {"modelRotationX", t.modelRotationX},
        {"modelRotationY", t.modelRotationY},
        {"modelOffsetX", t.modelOffsetX},
        {"modelOffsetY", t.modelOffsetY},
        {"modelAngleZ", t.modelAngleZ},
        {"resizeX", t.resizeX},
        {"resizeY", t.resizeY},
        {"resizeZ", t.resizeZ},
        {"ambient", t.ambient},
        {"contrast", t.contrast},
        {"shopPrice", t.shopPrice},
        {"geBuyLimit", t.geBuyLimit},
        {"category", t.category},
        {"searchable", t.searchable},
        {"isStackable", t.isStackable},
        {"multistackSize", t.multistackSize},
        {"notedID", t.notedID},
        {"templateID", t.templateID},
        {"neverStackable", t.neverStackable},
        {"wearpos", t.wearpos},
        {"wearpos2", t.wearpos2},
        {"wearpos3", t.wearpos3},
        {"maleModel1", t.maleModel1},
        {"maleModel2", t.maleModel2},
        {"maleModel3", t.maleModel3},
        {"femaleModel1", t.femaleModel1},
        {"femaleModel2", t.femaleModel2},
        {"femaleModel3", t.femaleModel3},
        {"maleHeadModel1", t.maleHeadModel1},
        {"maleHeadModel2", t.maleHeadModel2},
        {"femaleHeadModel1", t.femaleHeadModel1},
        {"femaleHeadModel2", t.femaleHeadModel2},
        {"maleModelOffsetX", t.maleModelOffsetX},
        {"maleModelOffsetY", t.maleModelOffsetY},
        {"maleModelOffsetZ", t.maleModelOffsetZ},
        {"femaleModelOffsetX", t.femaleModelOffsetX},
        {"femaleModelOffsetY", t.femaleModelOffsetY},
        {"femaleModelOffsetZ", t.femaleModelOffsetZ},
        {"originalColors", t.originalColors},
        {"replacementColors", t.replacementColors},
        {"originalTextures", t.originalTextures},
        {"replacementTextures", t.replacementTextures},
        {"recolorPalette", t.recolorPalette},
        {"isMembers", t.isMembers},
        {"isAllowedOnGE", t.isAllowedOnGE},
        {"randomizeGroundPos", t.randomizeGroundPos},
        {"teamId", t.teamId},
        {"lentItemId", t.lentItemId},
        {"lendTemplate", t.lendTemplate},
        {"bindId", t.bindId},
        {"boundTemplate", t.boundTemplate},
        {"shardItemId", t.shardItemId},
        {"shardTemplateId", t.shardTemplateId},
        {"shardCombineAmount", t.shardCombineAmount},
        {"shardName", t.shardName},
        {"groundCursors", groundCursors},
        {"inventoryCursors", invCursors},
        {"pickSizeShift", t.pickSizeShift},
        {"componentOptions", t.componentOptions},
        {"groundOptions", t.groundOptions},
        {"stackIDs", stacks},
        {"quests", t.quests},
        {"params", intMapToJson(t.params)},
    };
}

inline json toJson(const ParamType &t)
{
    json typeName = nullptr;
    if (t.type) typeName = t.type->getName();
    return {
        {"id", t.id},
        {"type", typeName},
        {"defaultString", t.defaultString},
        {"autoDisable", t.autoDisable},
        {"defaultInt", t.defaultInt},
    };
}

inline json toJson(const InventoryType &t)
{
    return {
        {"id", t.id},
        {"capacity", t.capacity},
        {"stackIds", t.stackIds},
        {"stackAmounts", t.stackAmounts},
    };
}

inline json toJson(const EnumType &t)
{
    return {
        {"id", t.id},
        {"inputTypeId", t.inputTypeId},
        {"outputTypeId", t.outputTypeId},
        {"intDefault", t.intDefault},
        {"stringDefault", t.stringDefault},
        {"entryCount", t.entryCount},
        {"entries", intMapToJson(t.entries)},
    };
}

inline json toJson(const StructType &t)
{
    return {
        {"id", t.id},
        {"params", intMapToJson(t.params)},
    };
}

inline json toJson(const SequenceType &t)
{
    return {
        {"id", t.id},
        {"frameLengths", t.frameLengths},
        {"frames", t.frames},
        {"secondaryFrames", t.secondaryFrames},
        {"loopOffset", t.loopOffset},
        {"priority", t.priority},
        {"offHand", t.offHand},
        {"mainHand", t.mainHand},
        {"maxLoops", t.maxLoops},
        {"animatingPrecedence", t.animatingPrecedence},
        {"walkingPrecedence", t.walkingPrecedence},
        {"replayMode", t.replayMode},
        {"tweened", t.tweened},
        {"newFramesId", t.newFramesId},
        {"params", intMapToJson(t.params)},
    };
}

inline json toJson(const QuestType &t)
{
    json varps = json::array();
    for (const auto &v: t.progressVarps) varps.push_back({v[0], v[1], v[2]});
    json varbits = json::array();
    for (const auto &v: t.progressVarbits) varbits.push_back({v[0], v[1], v[2]});
    json skills = json::array();
    for (const auto &[s, lvl]: t.skillRequirements) skills.push_back({{"skill", s}, {"level", lvl}});
    return {
        {"id", t.id},
        {"name", t.name},
        {"listName", t.listName},
        {"parentQuestId", t.parentQuestId},
        {"category", t.category},
        {"difficulty", t.difficulty},
        {"membersOnly", t.membersOnly},
        {"questPoints", t.questPoints},
        {"questPointReq", t.questPointReq},
        {"questItemSprite", t.questItemSprite},
        {"alternateStartLocation", t.alternateStartLocation},
        {"startLocations", t.startLocations},
        {"dependentQuestIds", t.dependentQuestIds},
        {"skillRequirements", skills},
        {"progressVarps", varps},
        {"progressVarbits", varbits},
        {"params", intMapToJson(t.params)},
    };
}

inline json toJson(const UnderlayType &t)
{
    return {
        {"id", t.id},
        {"color", t.color},
        {"texture", t.texture},
    };
}

inline json toJson(const OverlayType &t)
{
    return {
        {"id", t.id},
        {"color", t.color},
        {"secondaryColor", t.secondaryColor},
        {"texture", t.texture},
        {"visible", t.visible},
        {"isWater", t.isWater},
    };
}

inline json toJson(const WorldMapElementType &t)
{
    json coords = json::array();
    for (const auto &[x, y]: t.coordinates) coords.push_back({x, y});
    return {
        {"id", t.id},
        {"spriteId", t.spriteId},
        {"spriteId2", t.spriteId2},
        {"name", t.name},
        {"configRef", t.configRef},
        {"category", t.category},
        {"options", t.options},
        {"coordinates", coords},
        {"regionIds", t.regionIds},
        {"tooltip", t.tooltip},
        {"elementId", t.elementId},
        {"coord1", t.coord1},
        {"coord2", t.coord2},
        {"varbitId", t.varbitId},
        {"varpId", t.varpId},
        {"conditionMin", t.conditionMin},
        {"conditionMax", t.conditionMax},
        {"params", intMapToJson(t.params)},
    };
}

inline json toJson(const DbRowData &rd)
{
    json values = json::array();
    for (const auto &v: rd.values) values.push_back(variantToJson(v));
    return {
        {"columnTypeIds", rd.columnTypeIds},
        {"values", values},
    };
}

inline json toJson(const DbRowType &t)
{
    json rows = json::object();
    for (const auto &[idx, rd]: t.rows)
    {
        rows[std::to_string(idx)] = toJson(rd);
    }
    return {
        {"id", t.id},
        {"tableId", t.tableId},
        {"rows", rows},
    };
}

} // namespace nxtdump
