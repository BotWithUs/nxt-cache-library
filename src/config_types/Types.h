#pragma once

#include "config_types/ScriptVarType.h"
#include "core/RSBuffer.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class VarbitType
{
public:
    int id;
    int varId{};
    int domainType{};
    int msb{};
    int lsb{};

    explicit VarbitType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class NpcType
{
public:
    int id;
    std::string name;
    std::vector<std::string> options;
    std::vector<int> quests;
    std::vector<int> transforms;
    std::map<int, std::variant<int, std::string>> params;
    bool drawMapDot{};
    int combatLevel{};
    int varbitId{-1};
    int varpId{-1};
    int walkAnimationId{};
    int rotation{};
    bool isVisible = true;
    bool isClickable = true;
    bool animateIdle = true;
    int rotate180Animation{};
    int rotate90RightAnimation{};
    int rotate90LeftAnimation{};

    explicit NpcType() : id(-1), options(5) {}

    void decode(RSBuffer &buffer);
};

class LocationType
{
public:
    int id;
    std::string name{};
    std::vector<int> transforms{};
    std::vector<int> animations{};
    std::vector<std::string> options{};
    std::map<int, std::variant<int, std::string>> params{};
    int varbitId{-1};
    int varpId{-1};
    int sizeX{};
    int sizeY{};
    int interactType{};
    int solidType{2};
    int scaleX{};
    int scaleY{};
    int scaleZ{};
    int mapSpriteId{};
    int mapAreaId{-1};
    int cursors[6]{-1, -1, -1, -1, -1, -1};
    bool isMembers{};

    explicit LocationType() : id(-1), options(5) {}

    void decode(RSBuffer &buffer);
};

class ItemType
{
public:
    int id;

    int modelID{};
    int modelZoom{2000};
    int modelRotationX{};
    int modelRotationY{};
    int modelOffsetX{};
    int modelOffsetY{};
    int modelAngleZ{};
    int resizeX{};
    int resizeY{};
    int resizeZ{};
    int8_t ambient{};
    int contrast{};

    std::string name;
    std::string effect;
    int64_t shopPrice{1};
    int geBuyLimit{};
    int category{};
    uint8_t searchable{};

    bool isStackable{};
    int multistackSize{};
    int notedID{-1};
    int templateID{-1};
    bool neverStackable{};

    int8_t wearpos{-1};
    int8_t wearpos2{-1};
    int8_t wearpos3{-1};
    int maleModel1{-1};
    int maleModel2{-1};
    int maleModel3{-1};
    int femaleModel1{-1};
    int femaleModel2{-1};
    int femaleModel3{-1};
    int maleHeadModel1{-1};
    int maleHeadModel2{-1};
    int femaleHeadModel1{-1};
    int femaleHeadModel2{-1};
    int maleModelOffsetX{};
    int maleModelOffsetY{};
    int maleModelOffsetZ{};
    int femaleModelOffsetX{};
    int femaleModelOffsetY{};
    int femaleModelOffsetZ{};

    std::vector<uint16_t> originalColors;
    std::vector<uint16_t> replacementColors;
    std::vector<uint16_t> originalTextures;
    std::vector<uint16_t> replacementTextures;
    std::vector<int8_t> recolorPalette;

    bool op15Bool{};
    bool isMembers{};
    bool isAllowedOnGE{};
    bool randomizeGroundPos{};

    int8_t teamId{};
    int lentItemId{-1};
    int lendTemplate{-1};

    int bindId{-1};
    int boundTemplate{-1};

    int shardItemId{-1};
    int shardTemplateId{-1};
    uint16_t shardCombineAmount{};
    std::string shardName;

    int groundCursors[6]{-1, -1, -1, -1, -1, -1};
    int inventoryCursors[5]{-1, -1, -1, -1, -1};

    int pickSizeShift{};

    std::vector<std::string> componentOptions;
    std::vector<std::string> groundOptions;
    std::vector<std::pair<int, int>> stackIDs;
    std::vector<int> quests;
    std::map<int, std::variant<int, std::string>> params;

    explicit ItemType() : id(-1), componentOptions(5), groundOptions(5), stackIDs(10) {}

    void decode(RSBuffer &buffer);
};

class ParamType
{
public:
    int32_t id;
    ScriptVarType *type;
    std::string defaultString;
    bool autoDisable = true;
    int32_t defaultInt{};

    explicit ParamType() : id(-1), type(nullptr) {}

    void decode(RSBuffer &buffer);
};

class InventoryType
{
public:
    int id;
    int capacity;
    std::vector<int> stackIds{};
    std::vector<int> stackAmounts{};

    explicit InventoryType() : id(-1), capacity(0) {}

    void decode(RSBuffer &buffer);
};

class EnumType
{
public:
    int id;
    int inputTypeId{-1};
    int outputTypeId{-1};
    int intDefault{};
    std::string stringDefault;
    int entryCount{};
    std::map<int, std::variant<int, std::string>> entries;

    explicit EnumType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class StructType
{
public:
    int id;
    std::map<int, std::variant<int, std::string>> params;

    explicit StructType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class SequenceType
{
public:
    int id;
    std::vector<int> frameLengths;
    std::vector<int> frames;
    std::vector<int> secondaryFrames;
    int loopOffset{-1};
    int priority{};
    int offHand{-1};
    int mainHand{-1};
    int maxLoops{};
    int animatingPrecedence{};
    int walkingPrecedence{};
    int replayMode{};
    bool tweened{};
    int newFramesId{-1};
    std::map<int, std::variant<int, std::string>> params;

    explicit SequenceType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class QuestType
{
public:
    int id;
    std::string name;
    std::string listName;
    int parentQuestId{-1};
    int category{};
    int difficulty{};
    bool membersOnly{};
    int questPoints{};
    int questPointReq{};
    int questItemSprite{-1};
    int alternateStartLocation{};
    std::vector<int> startLocations;
    std::vector<int> dependentQuestIds;
    std::vector<std::pair<int, int>> skillRequirements;
    std::vector<std::array<int, 3>> progressVarps;
    std::vector<std::array<int, 3>> progressVarbits;
    std::map<int, std::variant<int, std::string>> params;

    explicit QuestType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class UnderlayType
{
public:
    int id;
    int color{0};
    int texture{-1};

    explicit UnderlayType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class OverlayType
{
public:
    int id;
    int color{0};
    int secondaryColor{-1};
    int texture{-1};
    bool visible{true};
    bool isWater{false};

    explicit OverlayType() : id(-1) {}

    void decode(RSBuffer &buffer);
};

class WorldMapElementType
{
public:
    int id;
    int spriteId{-1};
    int spriteId2{-1};
    std::string name;
    int configRef{-1};
    int category{0};
    std::vector<std::string> options;
    std::vector<std::pair<int16_t, int16_t>> coordinates;
    std::vector<int> regionIds;
    std::string tooltip;
    int elementId{-1};
    int coord1{0};
    int coord2{0};
    int varbitId{-1};
    int varpId{-1};
    int conditionMin{-1};
    int conditionMax{-1};
    std::map<int, std::variant<int, std::string>> params;

    explicit WorldMapElementType() : id(-1), options(5) {}

    void decode(RSBuffer &buffer);
};

using DbCellValue = std::variant<int, int64_t, std::string>;

struct DbRowData
{
    std::vector<int> columnTypeIds;
    std::vector<DbCellValue> values;
};

class DbRowType
{
public:
    int id;
    int tableId = -1;
    std::map<int, DbRowData> rows;

    explicit DbRowType() : id(-1) {}

    void decode(RSBuffer &buffer);

    [[nodiscard]] bool hasColumnType(int scriptVarTypeId) const;
    [[nodiscard]] std::vector<int> getIntValuesForColumnType(int scriptVarTypeId) const;

    [[nodiscard]] int getCol0IntKey() const;
    [[nodiscard]] const DbRowData *getRow(int rowIndex) const;
};
