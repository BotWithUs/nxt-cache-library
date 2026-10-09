#pragma once

#include "config_types/InterfaceTypes.h"
#include "config_types/ModelType.h"
#include "config_types/SpriteType.h"
#include "config_types/StatType.h"
#include "config_types/Types.h"
#include "config_types/VarPlayerType.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

inline json toJson(const StatType &s)
{
    return {
        {"id", s.id},
        {"maxLevel", s.maxLevel},
        {"baseLevel", s.baseLevel},
        {"flags", s.flags},
        {"capLevelRaw", s.capLevelRaw},
        {"capLevel", s.capLevel},
        {"capXpTenths", s.capXpTenths},
        {"xpTableIndex", s.xpTableIndex},
        {"trailingFlag", s.trailingFlag},
    };
}

inline json toJson(const StatDefaults &d)
{
    json stats = json::array();
    for (const auto &s : d.stats)
    {
        stats.push_back(toJson(s));
    }
    return {
        {"stats", stats},
        {"xpTables", d.xpTables},
        {"defaultXpTable", defaultStatXpTable()},
    };
}

inline json toJson(const VarPlayerType &t)
{
    json j = {
        {"id", t.id},
        {"typeId", t.typeId},
        {"flagOp7", t.flagOp7},
        {"flagOp8", t.flagOp8},
    };
    if (const ScriptVarType *svt = t.scriptVarType())
    {
        j["typeName"] = svt->getName();
    }
    if (t.hasOp4)
    {
        j["op4"] = t.op4;
    }
    if (t.hasOp5)
    {
        j["op5"] = t.op5;
    }
    if (t.hasOp110)
    {
        j["op110"] = t.op110;
    }
    const VarpDefault def = t.resolveDefault();
    if (def.rule != VarpDefaultRule::None)
    {
        j["defaultRule"] = def.rule == VarpDefaultRule::Domain ? "domain" : "type";
        if (def.text != nullptr)
        {
            j["default"] = *def.text;
        }
        else
        {
            j["default"] = def.value;
        }
    }
    return j;
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
        {"modelIds", t.modelIds},
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
        {"op182Value", t.op182Value},
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

inline json toJson(const ComponentColorState &cs)
{
    json j = {
        {"spriteId", static_cast<int32_t>(cs.color1)},
        {"color1", cs.color1},
        {"spritePart", cs.spritePart},
        {"flags", cs.flags},
        {"alpha", cs.alpha},
        {"priority", cs.priority},
        {"color2", cs.color2},
        {"color3", cs.color3},
    };
    if (cs.hasLegacyExtra)
    {
        j["legacyExtra"] = {cs.legacyExtra[0], cs.legacyExtra[1], cs.legacyExtra[2], cs.legacyExtra[3]};
    }
    return j;
}

inline json toJson(const ComponentHoverInfo &h)
{
    return {
        {"flags", h.flags},
        {"leftX", h.leftX},
        {"leftY", h.leftY},
        {"states", {toJson(h.states[0]), toJson(h.states[1]), toJson(h.states[2])}},
    };
}

inline json toJson(const ComponentTextInfo &t)
{
    return {
        {"textId", t.textId},
        {"flagBit0", t.flagBit0},
        {"text", t.text},
        {"fontSize", t.fontSize},
        {"fontStyle", t.fontStyle},
        {"fontEffect", t.fontEffect},
        {"flagBit1", t.flagBit1},
        {"color", t.color},
        {"alpha", t.alpha},
        {"shadow", t.shadow},
    };
}

inline json toJson(const ComponentOption &o)
{
    return {
        {"slot", o.slot},
        {"contentType", o.contentType},
        {"actionByte", o.actionByte},
        {"cursorByte", o.cursorByte},
    };
}

inline json toJson(const ComponentEventScript &e)
{
    return {{"eventId", e.eventId}, {"script", e.script}};
}

inline json toJson(const ComponentHook &h)
{
    json args = json::array();
    for (const auto &a : h.args)
    {
        if (a.type == 0)
        {
            args.push_back({{"type", a.type}, {"int", a.intValue}});
        }
        else if (a.type == 1)
        {
            args.push_back({{"type", a.type}, {"string", a.stringValue}});
        }
        else
        {
            args.push_back({{"type", a.type}});
        }
    }
    return {{"hookId", h.hookId}, {"scriptId", h.scriptId}, {"args", args}};
}

inline json labelledIds(const std::vector<std::pair<std::string, uint32_t>> &items)
{
    json out = json::array();
    for (const auto &p : items)
    {
        out.push_back({{"label", p.first}, {"id", p.second}});
    }
    return out;
}

inline json colorStates(const ComponentColorState *states, size_t count)
{
    json out = json::array();
    for (size_t i = 0; i < count; i++)
    {
        out.push_back(toJson(states[i]));
    }
    return out;
}

// Header fields after the type tail: ops, event tables, hooks, triggers.
inline void addExtendedHeader(json &j, const InterfaceComponentDef &c)
{
    json cursors = json::array();
    for (const auto &oc : c.opCursors)
    {
        cursors.push_back({{"index", oc.index}, {"cursor", oc.cursor}});
    }
    json strings = json::array();
    for (const auto &e : c.eventStrings)
    {
        strings.push_back({{"eventId", e.eventId}, {"flag", e.flag}, {"value", e.value}});
    }
    json hooks = json::array();
    for (const auto &h : c.hooks)
    {
        hooks.push_back(toJson(h));
    }
    j["opBase"] = c.opBase;
    j["ops"] = c.ops;
    j["opCursors"] = cursors;
    j["opSuffix"] = c.opSuffix;
    j["targetVerb"] = c.targetVerb;
    j["eventStrings"] = strings;
    j["hooks"] = hooks;
    j["triggers"] = {c.triggers[0], c.triggers[1], c.triggers[2], c.triggers[3], c.triggers[4]};
}

inline void addLayoutBlocks(json &j, const InterfaceComponentDef &c)
{
    switch (c.decoder)
    {
        case ComponentDecoder::Layer:
            j["layer"] = {
                {"scrollWidth", c.layerScrollWidth}, {"scrollHeight", c.layerScrollHeight},
                {"flagByte", c.layerFlagByte},
                {"shorts", {c.layerShorts[0], c.layerShorts[1], c.layerShorts[2], c.layerShorts[3]}},
                {"color", c.layerColor},
            };
            break;
        case ComponentDecoder::Box:
            j["box"] = {{"flag", c.boxFlag}};
            break;
        case ComponentDecoder::Text:
            j["text"] = toJson(c.text);
            break;
        case ComponentDecoder::Sprite:
            j["sprite"] = toJson(c.spriteState);
            break;
        case ComponentDecoder::Model:
            j["model"] = {
                {"id", c.modelId},
                {"flags", c.modelFlags},
                {"offsetX", c.modelOffsetX}, {"offsetY", c.modelOffsetY},
                {"rotateX", c.modelRotateX}, {"rotateY", c.modelRotateY}, {"rotateZ", c.modelRotateZ},
                {"originX", c.modelOriginX}, {"originY", 0}, {"originZ", 0},
                {"zoom", c.modelZoom},
                {"animId", c.modelAnimId},
                {"animSecondary", c.modelAnimSecondary},
                {"animTertiary", c.modelAnimTertiary},
            };
            break;
        case ComponentDecoder::Line:
            j["line"] = {{"thickness", c.lineThickness}, {"flag", c.lineFlag}};
            break;
        case ComponentDecoder::Panel:
            j["panel"] = {{"w", c.panelW}, {"h", c.panelH}, {"flag", c.panelFlag}, {"byte", c.panelByte}};
            break;
        case ComponentDecoder::Grid:
            j["grid"] = {
                {"cellW", c.gridCellW}, {"cellH", c.gridCellH}, {"hgap", c.gridHGap},
                {"cols", c.gridCols}, {"rows", c.gridRows}, {"flag", c.gridFlag},
            };
            break;
        case ComponentDecoder::RadialProgress:
            j["radialProgress"] = {{"sprite", toJson(c.radialSprite)}};
            break;
        default:
            break;
    }
}

inline void addControlBlocks(json &j, const InterfaceComponentDef &c)
{
    switch (c.decoder)
    {
        case ComponentDecoder::Button:
            j["button"] = {
                {"flag0", c.buttonFlag0}, {"shape", c.buttonShape}, {"variant", c.buttonVariant},
                {"toggle", c.buttonToggle}, {"clickable", c.buttonClickable},
                {"highlightColor", c.buttonHighlightColor},
                {"sprite", toJson(c.buttonSprite)},
                {"label", toJson(c.buttonLabel)},
            };
            break;
        case ComponentDecoder::Divider:
            j["divider"] = {
                {"flagA", c.dividerFlagA}, {"flagB", c.dividerFlagB},
                {"byteA", c.dividerByteA}, {"byteB", c.dividerByteB},
                {"sprite", toJson(c.dividerSprite)},
                {"label", toJson(c.dividerLabel)},
            };
            break;
        case ComponentDecoder::Input:
            j["input"] = {
                {"leadFlag", c.tailLeadFlag},
                {"maxLength", c.inputMaxLength},
                {"flagA", c.inputFlagA}, {"flagB", c.inputFlagB},
                {"cursorColor", c.inputCursorColor}, {"extraByte", c.inputExtraByte},
                {"sprite", toJson(c.inputSprite)},
                {"hover", toJson(c.inputHover)},
                {"label", toJson(c.inputLabel)},
            };
            break;
        case ComponentDecoder::Slider:
            j["slider"] = {
                {"flag", c.sliderFlag},
                {"track", c.sliderTrack}, {"fill", c.sliderFill}, {"handle", c.sliderHandle},
                {"shadow1", c.sliderShadow1}, {"shadow2", c.sliderShadow2},
                {"items", c.sliderItems},
                {"shorts", {c.sliderShorts[0], c.sliderShorts[1], c.sliderShorts[2],
                            c.sliderShorts[3], c.sliderShorts[4], c.sliderShorts[5]}},
                {"sprites", colorStates(c.sliderSprites, 3)},
                {"label", toJson(c.sliderLabel)},
            };
            break;
        default:
            break;
    }
}

inline void addChoiceBlocks(json &j, const InterfaceComponentDef &c)
{
    switch (c.decoder)
    {
        case ComponentDecoder::List:
            j["list"] = {
                {"flag", c.listFlag},
                {"bytes", {c.listBytes[0], c.listBytes[1], c.listBytes[2],
                           c.listBytes[3], c.listBytes[4], c.listBytes[5]}},
                {"items", labelledIds(c.listItems)},
                {"groups", c.listGroups},
                {"colorA", c.listColorA}, {"colorB", c.listColorB},
                {"sprites", colorStates(c.listSprites, 3)},
                {"hover", toJson(c.listHover)},
                {"label", toJson(c.listLabel)},
            };
            break;
        case ComponentDecoder::Combo:
            j["combo"] = {
                {"leadFlag", c.tailLeadFlag},
                {"bytes", {c.comboBytes[0], c.comboBytes[1], c.comboBytes[2], c.comboBytes[3]}},
                {"items", labelledIds(c.comboItems)},
                {"selectedId", c.comboSelectedId},
                {"trailingBytes", {c.comboTrailing[0], c.comboTrailing[1], c.comboTrailing[2], c.comboTrailing[3],
                                   c.comboTrailing[4], c.comboTrailing[5], c.comboTrailing[6], c.comboTrailing[7]}},
                {"sprites", colorStates(c.comboSprites, 3)},
                {"hover", toJson(c.comboHover)},
                {"label", toJson(c.comboLabel)},
            };
            break;
        case ComponentDecoder::RadioGroup:
            j["radioGroup"] = {
                {"flag", c.radioFlag},
                {"bytes", {c.radioBytes[0], c.radioBytes[1], c.radioBytes[2],
                           c.radioBytes[3], c.radioBytes[4], c.radioBytes[5]}},
                {"items", labelledIds(c.radioItems)},
                {"selected", c.radioSelected},
                {"colorA", c.radioColorA}, {"colorB", c.radioColorB},
                {"sprite", toJson(c.radioSprite)},
                {"label", toJson(c.radioLabel)},
            };
            break;
        case ComponentDecoder::GroupBox:
            j["groupBox"] = {
                {"w", c.groupBoxW}, {"h", c.groupBoxH}, {"flag", c.groupBoxFlag},
                {"bytes", {c.groupBoxBytes[0], c.groupBoxBytes[1], c.groupBoxBytes[2],
                           c.groupBoxBytes[3], c.groupBoxBytes[4]}},
                {"color", c.groupBoxColor},
                {"sprites", colorStates(c.groupBoxSprites, 2)},
                {"label", toJson(c.groupBoxLabel)},
            };
            break;
        default:
            break;
    }
}

inline void addPagedBlocks(json &j, const InterfaceComponentDef &c)
{
    const char *key = nullptr;
    switch (c.decoder)
    {
        case ComponentDecoder::PagedLayer:    key = "pagedLayer"; break;
        case ComponentDecoder::Carousel:      key = "carousel"; break;
        case ComponentDecoder::PagedCarousel: key = "pagedCarousel"; break;
        default: return;
    }
    j[key] = {
        {"flag", c.pageFlag},
        {"count", c.pageCount},
        {"bytes", {c.pageBytes[0], c.pageBytes[1], c.pageBytes[2], c.pageBytes[3]}},
        {"labels", c.pageLabels},
        {"ids", c.pageIds},
        {"values", c.pageValues},
        {"shorts", {c.pageShorts[0], c.pageShorts[1], c.pageShorts[2], c.pageShorts[3]}},
        {"colorA", c.pageColorA}, {"colorB", c.pageColorB},
        {"intA", c.pageIntA}, {"intB", c.pageIntB},
        {"sprites", colorStates(c.pageSprites, 3)},
        {"label", toJson(c.pageLabel)},
    };
}

inline void addOtherBlocks(json &j, const InterfaceComponentDef &c)
{
    if (c.decoder == ComponentDecoder::Component19)
    {
        j["component19"] = {
            {"flagA", c.c19FlagA},
            {"shorts", {c.c19Shorts[0], c.c19Shorts[1], c.c19Shorts[2],
                        c.c19Shorts[3], c.c19Shorts[4], c.c19Shorts[5]}},
            {"flag", c.c19Flag},
            {"colorA", c.c19ColorA}, {"colorB", c.c19ColorB},
            {"sprites", colorStates(c.c19Sprites, 3)},
            {"label", toJson(c.c19Label)},
        };
    }
    else if (c.decoder == ComponentDecoder::CrmView)
    {
        json m = json::object();
        for (const auto &p : c.crmMap)
        {
            m[std::to_string(p.first)] = p.second;
        }
        j["crmView"] = {
            {"flagA", c.crmFlagA},
            {"intList", c.crmIntList},
            {"string", c.crmString},
            {"flag", c.crmFlag},
            {"map", m},
            {"string1", c.crmString1},
            {"string2", c.crmString2},
        };
    }
    else if (c.decoder == ComponentDecoder::Component28)
    {
        json entries = json::array();
        for (const auto &e : c.c28Entries)
        {
            entries.push_back({
                {"key", e.key},
                {"position", {e.position[0], e.position[1], e.position[2]}},
                {"rotation", {e.rotation[0], e.rotation[1], e.rotation[2]}},
                {"scale",    {e.scale[0],    e.scale[1],    e.scale[2]}},
                {"misc",     {e.misc1, e.misc2, e.misc3}},
                {"color",    {e.color[0], e.color[1], e.color[2], e.color[3]}},
                {"flagA", e.flagA}, {"flagB", e.flagB},
            });
        }
        j["component28"] = {{"entries", entries}, {"trailing", c.c28Trailing}};
    }
}

// Keys up to "tailAlpha" are the pre-950 set, kept with corrected values
// (componentType is now byte 1 & 0x7F; byte 0 is "version"). Everything after
// is additive. The per-type block is chosen by the client's real decoder.
inline json toJson(const InterfaceComponentDef &c)
{
    json options = json::array();
    for (const auto &o : c.options) options.push_back(toJson(o));
    json scripts = json::array();
    for (const auto &e : c.eventScripts) scripts.push_back(toJson(e));

    json j = {
        {"id", c.id},
        {"componentType", c.componentType},
        {"rawTypeByte", c.rawTypeByte},
        {"flagsByte", c.flagsByte},
        {"debugName", c.debugName},
        {"subtype", c.subtype},
        {"rawX", c.rawX},
        {"rawY", c.rawY},
        {"rawWidth", c.rawWidth},
        {"rawHeight", c.rawHeight},
        {"aspectWidth", c.aspectWidth},
        {"aspectHeight", c.aspectHeight},
        {"widthMode", c.widthMode},
        {"heightMode", c.heightMode},
        {"xMode", c.xMode},
        {"yMode", c.yMode},
        {"parentScopeId", c.parentScopeId},
        {"interactFlag", c.interactFlag},
        {"triggerScriptRef", c.triggerScriptRef},
        {"miscByte", c.miscByte},
        {"eventMask", c.eventMask},
        {"options", options},
        {"cursorPrimary", c.cursorPrimary},
        {"cursorSecondary", c.cursorSecondary},
        {"cursorTertiary", c.cursorTertiary},
        {"eventScopeId", c.eventScopeId},
        {"eventScopeMin", c.eventScopeMin},
        {"eventScopeMax", c.eventScopeMax},
        {"eventChainId", c.eventChainId},
        {"eventScripts", scripts},
        {"tailColor", c.tailColor},
        {"tailAlpha", c.tailAlpha},
        {"version", c.version},
        {"rawVersionByte", c.rawVersionByte},
        {"agentType", c.agentType},
        {"typeName", componentTypeName(c.componentType, c.subtype)},
        {"hidden", c.isHidden},
        {"parentId", c.parentScopeId == 0xFFFF ? -1 : c.parentScopeId},
    };
    addExtendedHeader(j, c);
    if (!c.isDecoded)
    {
        j["decodeError"] = c.decodeError;
    }
    if (c.isTailTruncated)
    {
        j["tailTruncated"] = true;
    }
    addLayoutBlocks(j, c);
    addControlBlocks(j, c);
    addChoiceBlocks(j, c);
    addPagedBlocks(j, c);
    addOtherBlocks(j, c);
    return j;
}

inline json toJson(const InterfaceDef &d)
{
    json comps = json::object();
    for (const auto &[fid, comp] : d.components)
    {
        comps[std::to_string(fid)] = toJson(comp);
    }
    return {
        {"id", d.id},
        {"components", comps},
    };
}

// Sprites and models serialise to metadata only — the bulk pixel/geometry data
// is impractical as JSON and is exposed through the raw C-ABI getters instead.

inline json toJson(const SpriteType &s)
{
    json frames = json::array();
    for (const auto &f : s.frames)
    {
        frames.push_back({
            {"offsetX", f.offsetX}, {"offsetY", f.offsetY},
            {"width", f.width}, {"height", f.height},
        });
    }
    return {
        {"id", s.id},
        {"canvasWidth", s.canvasWidth},
        {"canvasHeight", s.canvasHeight},
        {"paletteCount", s.paletteCount},
        {"frameCount", static_cast<int>(s.frames.size())},
        {"frames", frames},
    };
}

inline json toJson(const ModelType &m)
{
    // Distinct material args referenced across render submeshes (→ JS5 index 26).
    json materials = json::array();
    std::vector<int> seen;
    for (const auto &r : m.renders)
    {
        if (r.materialArgument == 0) continue;
        if (std::find(seen.begin(), seen.end(), r.materialArgument) == seen.end())
        {
            seen.push_back(r.materialArgument);
            materials.push_back(r.materialArgument);
        }
    }
    return {
        {"id", m.id},
        {"format", m.format},
        {"version", m.version},
        {"meshCount", static_cast<int>(m.renders.size())},
        {"vertexCount", m.vertexCount},
        {"faceCount", m.totalFaces},
        {"hasSkins", m.hasSkin},
        {"hasColors", !m.vertexColors.empty()},
        {"materialArgs", materials},
        {"bbox", {
            {"minX", m.minX}, {"maxX", m.maxX},
            {"minY", m.minY}, {"maxY", m.maxY},
            {"minZ", m.minZ}, {"maxZ", m.maxZ},
        }},
    };
}

} // namespace nxtdump
