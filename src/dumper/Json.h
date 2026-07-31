#pragma once

#include "config_types/InterfaceTypes.h"
#include "config_types/ModelType.h"
#include "config_types/SpriteType.h"
#include "config_types/Types.h"

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
    };

    // Type-specific blocks — emit only the one relevant to this componentType
    // so the JSON stays readable.
    int ct = c.componentType;
    if (ct == 5 || ct == 9 || ct == 16 || ct == 17 || ct == 27)
    {
        j["sprite"] = toJson(c.spriteState);
    }
    else if (ct == 6)
    {
        j["model"] = {
            {"id", c.modelId},
            {"flags", c.modelFlags},
            {"offsetX", c.modelOffsetX}, {"offsetY", c.modelOffsetY},
            {"rotateX", c.modelRotateX}, {"rotateY", c.modelRotateY}, {"rotateZ", c.modelRotateZ},
            {"originX", c.modelOriginX}, {"originY", c.modelOriginY}, {"originZ", c.modelOriginZ},
            {"zoom", c.modelZoom},
            {"animId", c.modelAnimId},
            {"animSecondary", c.modelAnimSecondary},
            {"animTertiary", c.modelAnimTertiary},
        };
    }
    else if (ct == 10)
    {
        j["button"] = {
            {"toggle", c.buttonToggle},
            {"clickable", c.buttonClickable},
            {"highlightColor", c.buttonHighlightColor},
            {"hover", toJson(c.buttonHover)},
            {"label", toJson(c.buttonLabel)},
        };
    }
    else if (ct == 11 || ct == 15)
    {
        json items = json::array();
        for (const auto &p : c.listItems) items.push_back({{"label", p.first}, {"id", p.second}});
        j["list"] = {
            {"bytes", {c.listBytes[0],c.listBytes[1],c.listBytes[2],c.listBytes[3],
                       c.listBytes[4],c.listBytes[5],c.listBytes[6],c.listBytes[7]}},
            {"items", items},
            {"groups", c.listGroups},
            {"colorA", c.listColorA}, {"colorB", c.listColorB},
            {"hover", toJson(c.listHover)},
            {"label", toJson(c.listLabel)},
        };
    }
    else if (ct == 12)
    {
        json items = json::array();
        for (const auto &p : c.comboItems) items.push_back({{"label", p.first}, {"id", p.second}});
        j["combo"] = {
            {"byte777", c.combo777}, {"byte776", c.combo776},
            {"byte778", c.combo778}, {"byte779", c.combo779},
            {"items", items},
            {"selectedId", c.comboSelectedId},
            {"trailingBytes", {c.comboBytes[0],c.comboBytes[1],c.comboBytes[2],c.comboBytes[3],
                               c.comboBytes[4],c.comboBytes[5],c.comboBytes[6],c.comboBytes[7]}},
            {"hover", toJson(c.comboHover)},
            {"label", toJson(c.comboLabel)},
        };
    }
    else if (ct == 13)
    {
        j["input"] = {
            {"maxLength", c.inputMaxLength},
            {"flagA", c.inputFlagA}, {"flagB", c.inputFlagB},
            {"cursorColor", c.inputCursorColor},
            {"hover", toJson(c.inputHover)},
            {"label", toJson(c.inputLabel)},
        };
    }
    else if (ct == 14)
    {
        j["slider"] = {
            {"track", c.sliderTrack}, {"fill", c.sliderFill},
            {"handle", c.sliderHandle},
            {"shadow1", c.sliderShadow1}, {"shadow2", c.sliderShadow2},
            {"items", c.sliderItems},
            {"shorts", {c.sliderShorts[0], c.sliderShorts[1], c.sliderShorts[2],
                        c.sliderShorts[3], c.sliderShorts[4], c.sliderShorts[5]}},
            {"label", toJson(c.sliderLabel)},
        };
    }
    else if (ct == 18 || ct == 20 || ct == 21)
    {
        j["pages"] = {
            {"labels", c.pageLabels},
            {"ids", c.pageIds},
            {"bytes", {c.pageBytes[0],c.pageBytes[1],c.pageBytes[2],c.pageBytes[3],
                       c.pageBytes[4],c.pageBytes[5],c.pageBytes[6],c.pageBytes[7]}},
            {"shortsA", {c.pageShortsA[0],c.pageShortsA[1],c.pageShortsA[2],c.pageShortsA[3]}},
            {"shortsB", {c.pageShortsB[0],c.pageShortsB[1],c.pageShortsB[2],c.pageShortsB[3]}},
            {"colorA", c.pageColorA},
            {"colorB", c.pageColorB},
        };
    }
    else if (ct == 19)
    {
        j["component19"] = {
            {"shorts", {c.c19Shorts[0],c.c19Shorts[1],c.c19Shorts[2],
                        c.c19Shorts[3],c.c19Shorts[4],c.c19Shorts[5]}},
            {"flag", c.c19Flag},
            {"colorA", c.c19ColorA}, {"colorB", c.c19ColorB},
        };
    }
    else if (ct == 22)
    {
        json items = json::array();
        for (const auto &p : c.radioItems) items.push_back({{"label", p.first}, {"id", p.second}});
        j["radioGroup"] = {
            {"bytes", {c.radioBytes[0],c.radioBytes[1],c.radioBytes[2],
                       c.radioBytes[3],c.radioBytes[4],c.radioBytes[5]}},
            {"items", items},
            {"selected", c.radioSelected},
            {"colorA", c.radioColorA}, {"colorB", c.radioColorB},
        };
    }
    else if (ct == 23)
    {
        j["groupBox"] = {
            {"w", c.groupBoxW}, {"h", c.groupBoxH},
            {"flag", c.groupBoxFlag},
            {"bytes", {c.groupBoxBytes[0],c.groupBoxBytes[1],c.groupBoxBytes[2],
                       c.groupBoxBytes[3],c.groupBoxBytes[4]}},
            {"color", c.groupBoxColor},
            {"label", toJson(c.groupBoxLabel)},
        };
    }
    else if (ct == 26)
    {
        json m = json::object();
        for (const auto &p : c.crmMap) m[std::to_string(p.first)] = p.second;
        j["crmView"] = {
            {"intList", c.crmIntList},
            {"string", c.crmString},
            {"flag", c.crmFlag},
            {"map", m},
            {"string1", c.crmString1},
            {"string2", c.crmString2},
        };
    }
    else if (ct == 28)
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
    // Subtype-driven layer variants on type 0/3/4
    if (c.subtype == 1337 || c.subtype == 1403)
    {
        j["box"] = {{"flag", c.boxFlag}};
    }
    else if (c.subtype == 1338)
    {
        j["cutScene"] = {
            {"w", c.cutSceneW}, {"h", c.cutSceneH},
            {"color", c.cutSceneColor},
            {"shorts", {c.cutSceneShorts[0],c.cutSceneShorts[1],
                        c.cutSceneShorts[2],c.cutSceneShorts[3]}},
            {"toggle", c.cutSceneToggle},
        };
    }
    else if (c.subtype == 1400)
    {
        j["grid"] = {
            {"cellW", c.gridCellW}, {"cellH", c.gridCellH},
            {"hgap", c.gridHGap},
            {"cols", c.gridCols}, {"rows", c.gridRows},
            {"flag", c.gridFlag},
        };
    }
    else if (c.subtype == 1401)
    {
        j["panel"] = {
            {"w", c.panelW}, {"h", c.panelH},
            {"flag", c.panelFlag}, {"byte", c.panelByte},
        };
    }
    else if (c.subtype == 1405)
    {
        j["line"] = {{"thickness", c.lineThickness}, {"flag", c.lineFlag}};
    }
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
