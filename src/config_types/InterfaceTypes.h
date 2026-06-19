#pragma once

#include "core/RSBuffer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Component types as dispatched by jag::ComponentFactory::CreateComponent
// (verified against IDA on rs2client.exe build 948-5). The numbers are the
// first byte of each component's cache file.
enum class ComponentType : int
{
    Empty             = -1,   // raw byte 0xFF
    Layer             = 0,
    LayerWithBg       = 3,    // background-colored container
    Layer4            = 4,    // panel-like layer
    Sprite            = 5,
    Model             = 6,
    Sprite9           = 9,    // bordered sprite variant
    Button            = 10,
    List11            = 11,
    Combo             = 12,
    Input             = 13,
    Slider            = 14,
    List              = 15,
    Sprite16          = 16,   // dynamic sprite variant
    Sprite17          = 17,   // another sprite variant
    PagedLayer        = 18,
    Component19       = 19,   // generic colored container (gradient panel)
    Carousel          = 20,
    PagedCarousel     = 21,
    RadioGroup        = 22,
    GroupBox          = 23,
    RadialProgress    = 24,
    CRMView           = 26,
    Sprite27          = 27,
    Component28       = 28,
};

// One sub-record of "color + flags + timer" data; appears 3-4 times per
// component (normal/hover/click states). Decoded by sub_289D30. 16 bytes
// (legacy) or 20 bytes (componentType >= 6).
struct ComponentColorState
{
    uint32_t color1{};       // +0  4 bytes BE
    int spritePart{};        // +8  2 bytes BE (sprite index, animation id, etc.)
    uint8_t flags{};         // +21 bits set from up to 5 flag bytes
    uint8_t alpha{};         // +16 ~negated byte
    uint8_t priority{};      // +10 1 byte
    uint32_t color2{};       // +4  4 bytes BE-swap
    uint32_t color3{};       // +12 4 bytes BE-swap
    uint8_t legacyExtra[4]{};// +17..+20 only if componentType >= 6
    bool hasLegacyExtra{};
};

// Sub-record decoded by sub_288DA0 — appears on Button/Input/Combo/List/Slider
// tails. Carries hover/click flags + left-justify offsets + 3 color states.
struct ComponentHoverInfo
{
    uint8_t flags{};         // +147
    uint8_t leftX{};         // +144
    uint8_t leftY{};         // +145
    ComponentColorState states[3]{};
};

// Sub-record decoded by jag::Component::TextComponent::Decode — text body,
// font, color, alpha, shadow. Appears on Button/Combo/List/Slider/GroupBox.
struct ComponentTextInfo
{
    int textId{-1};          // +40 smart-int (script-resolved text id)
    uint8_t flagBit0{};      // bit 0 of +8
    std::string text;        // +16 inline text fallback
    uint8_t fontSize{};      // +44
    uint8_t fontStyle{};     // +46
    uint8_t fontEffect{};    // +47
    uint8_t flagBit1{};      // bit 1 of +8
    uint32_t color{};        // +48 BE-swap u32
    uint8_t alpha{};         // +52 ~negated byte
    uint8_t shadow{};        // +45 (only if componentType >= 0)
};

// One option slot from the common header's option list. The packed encoding
// gives 12 bits of content type id plus two trailing bytes (cursor + action
// id) per slot. Slot index is encoded in the high nibble of the packed byte.
struct ComponentOption
{
    int slot{};              // 1..15
    int contentType{-1};     // 12-bit id (4095 → -1)
    int actionByte{};        // first trailing byte
    int cursorByte{};        // second trailing byte
};

// One entry from sub_262DD0's "event scripts" table — keyed by a packed
// (3-byte id, 4-byte value) per row.
struct ComponentEventScript
{
    int eventId{};           // 3-byte BE id
    uint32_t script{};       // 4-byte BE-swap value
};

// One component within an interface. A single interface (archive in cache
// index 3) holds many of these (one per file id in the archive).
struct InterfaceComponentDef
{
    int id{-1};
    int componentType{-1};   // first byte (255 → -1)
    int rawTypeByte{-1};     // verbatim (for blank slots == 255 vs valid 0..28)
    int flagsByte{};         // second byte
    std::string debugName;   // only if flagsByte & 0x80
    int subtype{};           // 2 bytes BE after name

    // Common header (jag::Component::DecodeType)
    int rawX{}, rawY{}, rawWidth{}, rawHeight{};
    int aspectWidth{}, aspectHeight{};
    int widthMode{}, heightMode{}, xMode{}, yMode{};
    int parentScopeId{};     // 2 bytes BE at struct +14
    int interactFlag{};
    int triggerScriptRef{-1};// 4 bytes BE (only when componentType >= 6)
    int miscByte{};          // 1 byte (only when componentType >= 9)
    uint32_t eventMask{};    // 3 bytes legacy / 4 bytes modern
    std::vector<ComponentOption> options;
    int cursorPrimary{}, cursorSecondary{}, cursorTertiary{};
    int eventScopeId{-1};    // only if eventMask & 0x3F800
    int eventScopeMin{};
    int eventScopeMax{};
    int eventChainId{-1};    // only if componentType >= 0
    std::vector<ComponentEventScript> eventScripts;

    // Type-specific tail fields. Only those relevant to componentType are set.
    // (We keep them all on one flat struct rather than juggling 22 variant
    // arms — matches the established Types.h pattern for ItemType/NpcType.)

    // Shared visual tail (most components end with these two)
    uint32_t tailColor{};    // +96 BE-swap u32
    uint8_t tailAlpha{};     // +100 ~negated byte

    // Sprite (type 5) + Sprite-variant tails — sub_289D30 result mirrored
    // into tailColor/tailAlpha.
    ComponentColorState spriteState;

    // Model (type 6)
    int modelId{-1};         // smartInt
    int modelFlags{};
    int modelOffsetX{}, modelOffsetY{};
    int modelRotateX{}, modelRotateY{}, modelRotateZ{};
    int modelOriginX{}, modelOriginY{}, modelOriginZ{};
    int modelZoom{};
    int modelAnimId{-1};
    int modelAnimSecondary{-1};
    int modelAnimTertiary{-1};

    // Button (type 10)
    bool buttonToggle{};
    bool buttonClickable{};
    uint32_t buttonHighlightColor{};
    ComponentHoverInfo buttonHover;
    ComponentTextInfo buttonLabel;

    // Combo (type 12)
    uint8_t combo777{}, combo776{}, combo778{}, combo779{};
    std::vector<std::pair<std::string,uint32_t>> comboItems; // (label, id)
    int comboSelectedId{};
    uint8_t comboBytes[8]{};
    ComponentHoverInfo comboHover;
    ComponentTextInfo comboLabel;

    // Input (type 13)
    int inputMaxLength{};
    uint8_t inputFlagA{}, inputFlagB{};
    uint32_t inputCursorColor{};
    ComponentHoverInfo inputHover;
    ComponentTextInfo inputLabel;

    // Slider (type 14)
    uint32_t sliderTrack{}, sliderFill{};
    uint32_t sliderHandle{}, sliderShadow1{}, sliderShadow2{};
    std::vector<std::string> sliderItems;
    uint16_t sliderShorts[6]{};
    ComponentTextInfo sliderLabel;

    // List (type 15 and type 11)
    uint8_t listBytes[8]{};
    std::vector<std::pair<std::string,uint32_t>> listItems;
    std::vector<int> listGroups;
    uint32_t listColorA{}, listColorB{};
    ComponentHoverInfo listHover;
    ComponentTextInfo listLabel;

    // PagedLayer (type 18) / PagedCarousel (type 21) / Carousel (type 20)
    std::vector<std::string> pageLabels;
    std::vector<uint32_t> pageIds;
    uint8_t pageBytes[8]{};
    uint16_t pageShortsA[4]{};
    uint16_t pageShortsB[4]{};
    uint32_t pageColorA{}, pageColorB{};

    // GroupBox (type 23)
    uint16_t groupBoxW{}, groupBoxH{};
    uint8_t groupBoxFlag{};
    uint8_t groupBoxBytes[5]{};
    uint32_t groupBoxColor{};
    ComponentTextInfo groupBoxLabel;

    // Grid (a layer subtype 1400)
    uint16_t gridCellW{}, gridCellH{};
    uint8_t gridHGap{};
    uint16_t gridCols{}, gridRows{};
    bool gridFlag{};

    // Line (subtype 1405)
    uint8_t lineThickness{};
    bool lineFlag{};

    // Panel (subtype 1401)
    uint16_t panelW{}, panelH{};
    bool panelFlag{};
    uint8_t panelByte{};

    // RadialProgressOverlay (type 24)
    // (no extra fields beyond shared tail color/alpha and one nested hover)

    // RadioGroup (type 22)
    uint8_t radioBytes[6]{};
    std::vector<std::pair<std::string,uint32_t>> radioItems;
    int radioSelected{};
    uint32_t radioColorA{}, radioColorB{};

    // Box (subtype 1337/1403)
    bool boxFlag{};

    // CRMView (type 26)
    std::vector<uint32_t> crmIntList;
    std::string crmString;
    uint8_t crmFlag{};
    std::map<uint32_t,std::string> crmMap;
    std::string crmString1, crmString2;

    // Component19 (type 19)
    uint16_t c19Shorts[6]{};
    uint8_t  c19Flag{};
    uint32_t c19ColorA{}, c19ColorB{};

    // Component28 (type 28) — record list of per-key transform records.
    struct Component28Entry
    {
        int key{};
        float position[3]{};
        float rotation[3]{};
        float scale[3]{};
        float misc1{}, misc2{}, misc3{};
        float color[4]{};
        uint8_t flagA{};
        uint8_t flagB{};
    };
    std::vector<Component28Entry> c28Entries;
    uint8_t c28Trailing{};

    // CutScene (subtype 1338)
    uint16_t cutSceneW{}, cutSceneH{};
    uint32_t cutSceneColor{};
    uint16_t cutSceneShorts[4]{};
    bool cutSceneToggle{};

    void decode(RSBuffer &buffer);
};

// One whole interface (cache archive in JS5 index 3). Components are sparse —
// missing files map to absent slots.
struct InterfaceDef
{
    int id{-1};
    std::map<int, InterfaceComponentDef> components;
};
