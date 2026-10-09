#pragma once

#include "core/RSBuffer.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Interface component decoder, mirroring the 950-1 rs2client.exe loader.
// EAs below are image-base-relative (IDA base 0) in the 950-1 Windows build.
//
// File layout (jag::game::InterfaceList::GetInterface -> sub_2946B0):
//   byte 0   format version (0xFF reads as -1, so every "version >= N" gate is
//            false for it; the client still decodes the full component)
//   byte 1   bit 7 = a debug-name string follows; low 7 bits = factory type
//   [name]   NUL-terminated string when byte 1 & 0x80
//   u16      subtype (ComponentFactory::CreateComponent 0xBB850 checks it first)
//   header   sub_3401F0. The type-specific tail (vtable slot +72) is decoded in
//            the MIDDLE of the header, straight after interactFlag.

// Cache factory type (byte 1 & 0x7F). The numbers are the cache's; agentType
// below is what the injected agent's GetComponentType reports for each class
// (vtable +8 getter, verified per class).
//
// Names: Box/Text/Sprite/Model/Button/Divider/List/Input/Combo/Layer/
// CutScene follow NXTLibrary's offsets.h categories. The rest come from the
// IDB's carried-over decode-function names and are hints only. Type 11 is a
// known conflict: the agent calls its category CRMView, while the IDB names its
// decoder PanelComponent and gives "CRMView" to type 26's decoder. The binary is
// stripped (no RTTI), so neither name is confirmed.
enum class ComponentType : int
{
    Layer          = 0,
    Box            = 3,
    Text           = 4,
    Sprite         = 5,
    Model          = 6,
    Line           = 9,
    Button         = 10,
    Panel          = 11,   // agent: CRMView (see the conflict note above)
    Divider        = 12,
    Input          = 13,
    Slider         = 14,
    Grid           = 15,
    List           = 16,
    Combo          = 17,
    PagedLayer     = 18,
    Component19    = 19,
    Carousel       = 20,
    PagedCarousel  = 21,
    RadioGroup     = 22,
    GroupBox       = 23,
    RadialProgress = 24,
    CrmView        = 26,   // IDB name; agent leaves this category unnamed
    CutScene       = 27,
    Component28    = 28,
    Component29    = 29,   // no tail (decode slot is a no-op)
};

// Which tail decoder the client runs for a component, after the factory has
// applied its subtype overrides.
enum class ComponentDecoder : int
{
    Unknown = -1,
    None,          // type 29
    Layer,         // 0x272CE0: types 0, 27 and the type-0 subtypes
    Box,           // 0x289190
    Text,          // 0x28CCC0 -> TextComponent::Decode 0x2944E0
    Sprite,        // 0x28B910 -> sub_2923D0
    Model,         // 0x391880
    Line,          // 0x272F70
    Button,        // 0x25F030
    Panel,         // 0x27CD00
    Divider,       // 0x263DA0
    Input,         // 0x270BC0
    Slider,        // 0x289A90
    Grid,          // 0x26DED0
    List,          // 0x274320
    Combo,         // 0x268960
    PagedLayer,    // 0x27A2F0
    Component19,   // 0x27BB90
    Carousel,      // 0x262670
    PagedCarousel, // 0x277CC0
    RadioGroup,    // 0x288580
    GroupBox,      // 0x26E840
    RadialProgress,// 0x27DAE0
    CrmView,       // 0x260730
    Component28,   // 0x391CD0
};

// sub_2923D0: sprite / colour-state record. 19 bytes, +1 at version >= 3,
// +4 more at version >= 6. Field names predate the RE and are kept for the JSON.
struct ComponentColorState
{
    uint32_t color1{};       // u32: the sprite id (0xFFFFFFFF = none)
    int spritePart{};        // u16 (+34)
    uint8_t flags{};         // bit0/bit3 from the flag byte; bit1, bit2, bit4 from the bool bytes
    uint8_t alpha{};         // ~byte
    uint8_t priority{};      // byte (+33)
    uint32_t color2{};       // u32 (+40, stored << 8)
    uint32_t color3{};       // u32 (+36, combined with alpha)
    uint8_t legacyExtra[4]{};// version >= 6
    bool hasLegacyExtra{};
};

// sub_291360: two flag bytes, two bytes, three ColorState records.
struct ComponentHoverInfo
{
    uint8_t flags{};
    uint8_t leftX{};
    uint8_t leftY{};
    ComponentColorState states[3]{};
};

// jag::Component::TextComponent::Decode (0x2944E0).
struct ComponentTextInfo
{
    int textId{-1};          // smart (2-byte 0x7FFF = -1) at +32; likely the font id (unverified)
    uint8_t flagBit0{};      // version >= 2
    std::string text;
    uint8_t fontSize{};
    uint8_t fontStyle{};
    uint8_t fontEffect{};
    uint8_t flagBit1{};
    uint32_t color{};
    uint8_t alpha{};         // ~byte
    uint8_t shadow{};        // version >= 0
};

// One option slot from the header's packed option list.
struct ComponentOption
{
    int slot{};              // packed byte >> 4 (1-based)
    int contentType{-1};     // 12 bits, 0xFFF -> -1
    int actionByte{};
    int cursorByte{};
};

// sub_26B7A0, first table: 3-byte id + u32.
struct ComponentEventScript
{
    int eventId{};
    uint32_t script{};
};

// sub_26B7A0, second table: 3-byte id + flag byte; a string follows when the
// flag is 0.
struct ComponentEventString
{
    int eventId{};
    int flag{};
    std::string value;
};

// sub_26AF80: op cursor entry (index byte + u16).
struct ComponentOpCursor
{
    int index{};
    int cursor{};
};

// One argument of a hook (sub_33FE40): type 0 = int (u32), 1 = string; any
// other type byte carries no payload.
struct ComponentHookArg
{
    int type{};
    int32_t intValue{};
    std::string stringValue;
};

// One script hook (sub_33FE40). 'hookId' is the key sub_33FB80 passes for the
// slot (35, 4, 6, 16, ...); its meaning is not named here.
struct ComponentHook
{
    int hookId{};
    int scriptTypeByte{};    // the leading byte of entry 0 (read, not used by the client)
    int32_t scriptId{};      // entry 0's u32; -2 means "no script"
    std::vector<ComponentHookArg> args;
};

// One component within an interface (one file of an index-3 archive).
struct InterfaceComponentDef
{
    int id{-1};

    // File preamble
    int version{-1};         // byte 0, 0xFF -> -1
    int rawVersionByte{-1};  // byte 0 verbatim
    int componentType{-1};   // byte 1 & 0x7F: the factory type
    int rawTypeByte{-1};     // byte 1 verbatim (bit 7 = has debug name)
    int flagsByte{};         // byte 1 verbatim (kept for JSON compatibility)
    std::string debugName;
    int subtype{};
    int agentType{-1};       // the agent's category byte for the resolved class
    ComponentDecoder decoder{ComponentDecoder::Unknown};

    // Decode outcome. On failure the struct holds whatever was read up to the
    // point of failure and must not be trusted beyond the preamble.
    bool isDecoded{};
    std::string decodeError;
    int fileLength{};
    int bytesConsumed{};

    // Header, part 1 (before the tail)
    int rawX{}, rawY{}, rawWidth{}, rawHeight{};
    int widthMode{}, heightMode{}, xMode{}, yMode{};
    int aspectWidth{}, aspectHeight{};
    int parentScopeId{0xFFFF};  // component id within the same interface; 0xFFFF = root
    int interactFlag{};
    bool isHidden{};            // interactFlag bit 0

    // Header, part 2 (after the tail)
    int triggerScriptRef{-1};   // version >= 6
    int miscByte{};             // version >= 9
    uint32_t eventMask{};       // u32 at version >= 6, 3 bytes before
    std::vector<ComponentOption> options;
    std::string opBase;         // sub_26AF80
    std::vector<std::string> ops;
    std::vector<ComponentOpCursor> opCursors;
    std::string opSuffix;       // sub_26AF80's trailing string (semantics unverified)
    int cursorPrimary{}, cursorSecondary{}, cursorTertiary{};  // +305, +306, +304
    std::string targetVerb;     // string after the cursor bytes (name by OSRS analogy)
    int eventScopeId{-1};       // eventMask & 0x3F800
    int eventScopeMin{};
    int eventScopeMax{};
    int eventChainId{-1};       // version >= 0
    std::vector<ComponentEventScript> eventScripts;  // version >= 0
    std::vector<ComponentEventString> eventStrings;  // version >= 0
    std::vector<ComponentHook> hooks;
    std::vector<uint32_t> triggers[5];               // sub_340130 x5

    // Shared tail colour/alpha (most tails write +136/+140)
    uint32_t tailColor{};
    uint8_t tailAlpha{};
    bool tailLeadFlag{};        // input/combo leading bool byte (client: +65 bit 0)

    // Layer (0x272CE0)
    int layerScrollWidth{}, layerScrollHeight{};
    int layerFlagByte{-1};      // version == -1 only
    uint16_t layerShorts[4]{};  // 6 <= version < 9
    uint32_t layerColor{};      // version >= 9 (4 raw bytes, little-endian)

    // Box (3)
    bool boxFlag{};

    // Text (4, subtype 1405/1406)
    ComponentTextInfo text;

    // Sprite (5, subtype 1339)
    ComponentColorState spriteState;

    // Model (6)
    int modelId{-1};
    int modelFlags{};
    int modelOffsetX{}, modelOffsetY{};
    int modelRotateX{}, modelRotateY{}, modelRotateZ{};
    int modelOriginX{};          // the third offset in the flags & 2 form
    int modelZoom{};
    int modelAnimId{-1};
    int modelAnimSecondary{-1};  // u16, only when widthMode != 0 (semantics unverified)
    int modelAnimTertiary{-1};   // u16, only when heightMode != 0 (semantics unverified)

    // Line (9)
    uint8_t lineThickness{};
    bool lineFlag{};

    // Button (10)
    bool buttonFlag0{};
    uint8_t buttonShape{}, buttonVariant{};
    bool buttonToggle{};
    bool buttonClickable{};
    uint32_t buttonHighlightColor{};
    ComponentColorState buttonSprite;
    ComponentTextInfo buttonLabel;

    // Panel (11)
    int panelW{}, panelH{};
    bool panelFlag{};
    uint8_t panelByte{};

    // Divider (12)
    bool dividerFlagA{}, dividerFlagB{};
    uint8_t dividerByteA{}, dividerByteB{};
    ComponentColorState dividerSprite;
    ComponentTextInfo dividerLabel;

    // Input (13)
    uint8_t inputFlagA{}, inputFlagB{};
    int inputMaxLength{};
    uint32_t inputCursorColor{};  // version >= 9
    uint8_t inputExtraByte{};     // version >= 7
    ComponentColorState inputSprite;
    ComponentTextInfo inputLabel;
    ComponentHoverInfo inputHover;

    // Slider (14)
    bool sliderFlag{};
    uint32_t sliderTrack{}, sliderFill{}, sliderHandle{}, sliderShadow1{}, sliderShadow2{};
    std::vector<std::string> sliderItems;
    uint16_t sliderShorts[6]{};
    ComponentTextInfo sliderLabel;
    ComponentColorState sliderSprites[3];

    // Grid (15)
    int gridCellW{}, gridCellH{};
    uint8_t gridHGap{};
    int gridCols{}, gridRows{};
    bool gridFlag{};

    // List (16) and Combo (17). The client stops the tail early when the id
    // count does not equal the label count; 'isTailTruncated' records that.
    bool isTailTruncated{};
    bool listFlag{};
    uint8_t listBytes[6]{};       // [3] only at version >= 9
    std::vector<std::pair<std::string, uint32_t>> listItems;
    std::vector<int> listGroups;
    uint32_t listColorA{}, listColorB{};  // version >= 9
    ComponentColorState listSprites[3];
    ComponentTextInfo listLabel;
    ComponentHoverInfo listHover;

    uint8_t comboBytes[4]{};
    std::vector<std::pair<std::string, uint32_t>> comboItems;
    int comboSelectedId{};
    uint8_t comboTrailing[8]{};
    ComponentColorState comboSprites[3];
    ComponentTextInfo comboLabel;
    ComponentHoverInfo comboHover;

    // PagedLayer (18), Carousel (20), PagedCarousel (21)
    bool pageFlag{};
    int pageCount{};
    uint8_t pageBytes[4]{};
    std::vector<std::string> pageLabels;
    std::vector<uint32_t> pageIds;
    std::vector<uint32_t> pageValues;   // carousel: the second id list
    uint16_t pageShorts[4]{};
    uint32_t pageColorA{}, pageColorB{};
    uint32_t pageIntA{}, pageIntB{};
    ComponentColorState pageSprites[3];
    ComponentTextInfo pageLabel;

    // Component19 (19)
    bool c19FlagA{};
    uint16_t c19Shorts[6]{};
    bool c19Flag{};
    uint32_t c19ColorA{}, c19ColorB{};
    ComponentColorState c19Sprites[3];
    ComponentTextInfo c19Label;

    // RadioGroup (22)
    bool radioFlag{};
    uint8_t radioBytes[6]{};
    std::vector<std::pair<std::string, uint32_t>> radioItems;
    int radioSelected{};
    uint32_t radioColorA{}, radioColorB{};
    ComponentTextInfo radioLabel;
    ComponentColorState radioSprite;

    // GroupBox (23)
    int groupBoxW{}, groupBoxH{};
    uint8_t groupBoxFlag{};
    uint8_t groupBoxBytes[5]{};
    uint32_t groupBoxColor{};
    ComponentTextInfo groupBoxLabel;
    ComponentColorState groupBoxSprites[2];

    // RadialProgress (24)
    ComponentColorState radialSprite;

    // CrmView (26)
    bool crmFlagA{};
    std::vector<uint32_t> crmIntList;
    std::string crmString;
    uint8_t crmFlag{};
    std::vector<std::pair<uint32_t, std::string>> crmMap;
    std::string crmString1, crmString2;   // version >= 11

    // Component28 (28)
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

    void decode(RSBuffer &buffer);
};

// One whole interface (cache archive in JS5 index 3). Components are sparse.
struct InterfaceDef
{
    int id{-1};
    std::map<int, InterfaceComponentDef> components;
};

// Display name for a cache factory type ("layer", "text", ...; "type<N>" when
// unnamed). Used by the JSON exports.
const char *componentTypeName(int componentType, int subtype);
