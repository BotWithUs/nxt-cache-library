// Interface component decoder. Mirrors the 950-1 rs2client.exe loader read for
// read; see InterfaceTypes.h for the file layout and the EA of every decoder.
// Each function below names the client function it reproduces.

#include "config_types/InterfaceTypes.h"

#include <cstdint>
#include <cstring>

namespace
{

// Bounds-checked view over the component buffer. The first read that would
// run past the end marks the reader overrun and every later read returns 0,
// so a desync is detected instead of silently producing zeros.
class Reader
{
public:
    explicit Reader(RSBuffer &buffer) : buffer(buffer)
    {
    }

    [[nodiscard]] bool isOverrun() const
    {
        return overrun;
    }

    int u8()
    {
        if (!need(1))
        {
            return 0;
        }
        return buffer.readUnsignedByte();
    }

    bool flag()
    {
        return u8() == 1;
    }

    uint8_t alpha()
    {
        return static_cast<uint8_t>(~u8());
    }

    int u16()
    {
        if (!need(2))
        {
            return 0;
        }
        return buffer.readUnsignedShort();
    }

    uint32_t u32()
    {
        if (!need(4))
        {
            return 0;
        }
        return static_cast<uint32_t>(buffer.readInt());
    }

    int medium()
    {
        if (!need(3))
        {
            return 0;
        }
        return buffer.readMediumInt();
    }

    // Four raw bytes assembled little-endian (the client stores them as-is).
    uint32_t le32()
    {
        if (!need(4))
        {
            return 0;
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; i++)
        {
            value |= static_cast<uint32_t>(buffer.readUnsignedByte()) << (8 * i);
        }
        return value;
    }

    // sub_FEEA0: u16 when the top bit is clear (0x7FFF -> -1), else u32 & 0x7FFFFFFF.
    int smart()
    {
        if (!need(1))
        {
            return 0;
        }
        auto peek = static_cast<unsigned char>(buffer.buffer[buffer.readPosition]);
        if (peek <= 0x7F)
        {
            int value = u16();
            return value == 0x7FFF ? -1 : value;
        }
        return static_cast<int>(u32() & 0x7FFFFFFFu);
    }

    float f32()
    {
        uint32_t bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::string str()
    {
        if (overrun)
        {
            return {};
        }
        const void *end = nullptr;
        if (buffer.readPosition < buffer.writePosition)
        {
            end = std::memchr(buffer.buffer + buffer.readPosition, 0,
                              buffer.writePosition - buffer.readPosition);
        }
        if (end == nullptr)
        {
            overrun = true;
            return {};
        }
        return buffer.readString();
    }

    void skip(int length)
    {
        if (need(static_cast<size_t>(length)))
        {
            buffer.skip(length);
        }
    }

private:
    bool need(size_t n)
    {
        if (overrun || !buffer.canRead(n))
        {
            overrun = true;
            return false;
        }
        return true;
    }

    RSBuffer &buffer;
    bool overrun{};
};

// sub_2923D0
void readColorState(Reader &r, ComponentColorState &cs, int version)
{
    cs.color1 = r.u32();
    cs.spritePart = r.u16();
    int fb = r.u8();
    cs.flags = static_cast<uint8_t>((fb & 1) | ((fb & 2) << 2));
    cs.alpha = r.alpha();
    cs.priority = static_cast<uint8_t>(r.u8());
    cs.color2 = r.u32();
    if (r.flag())
    {
        cs.flags |= 0x02;
    }
    if (r.flag())
    {
        cs.flags |= 0x04;
    }
    cs.color3 = r.u32();
    if (version >= 3 && r.flag())
    {
        cs.flags |= 0x10;
    }
    if (version >= 6)
    {
        cs.hasLegacyExtra = true;
        for (auto &b : cs.legacyExtra)
        {
            b = static_cast<uint8_t>(r.u8());
        }
    }
}

// sub_291360
void readHover(Reader &r, ComponentHoverInfo &h, int version)
{
    if (r.flag())
    {
        h.flags |= 0x01;
    }
    if (r.flag())
    {
        h.flags |= 0x02;
    }
    h.leftX = static_cast<uint8_t>(r.u8());
    h.leftY = static_cast<uint8_t>(r.u8());
    for (auto &state : h.states)
    {
        readColorState(r, state, version);
    }
}

// jag::Component::TextComponent::Decode (0x2944E0)
void readTextInfo(Reader &r, ComponentTextInfo &t, int version)
{
    t.textId = r.smart();
    if (version >= 2)
    {
        t.flagBit0 = static_cast<uint8_t>(r.u8() == 1);
    }
    t.text = r.str();
    t.fontSize = static_cast<uint8_t>(r.u8());
    t.fontStyle = static_cast<uint8_t>(r.u8());
    t.fontEffect = static_cast<uint8_t>(r.u8());
    t.flagBit1 = static_cast<uint8_t>(r.u8() == 1);
    t.color = r.u32();
    t.alpha = r.alpha();
    if (version >= 0)
    {
        t.shadow = static_cast<uint8_t>(r.u8());
    }
}

// The "alpha byte + u32 colour" pair most tails end their fixed part with.
void readTailColor(Reader &r, InterfaceComponentDef &c)
{
    c.tailAlpha = r.alpha();
    c.tailColor = r.u32();
}

std::vector<std::string> readStrings(Reader &r, int count)
{
    std::vector<std::string> out;
    for (int i = 0; i < count && !r.isOverrun(); i++)
    {
        out.push_back(r.str());
    }
    return out;
}

// ------------------------------------------------------------------ tails ----

// 0x272CE0 (types 0, 27 and the type-0 subtypes)
void tailLayer(Reader &r, InterfaceComponentDef &c, int v)
{
    c.layerScrollWidth = r.u16();
    c.layerScrollHeight = r.u16();
    if (v == -1)
    {
        c.layerFlagByte = r.u8();
    }
    else if (v >= 9)
    {
        c.layerColor = r.le32();
    }
    else if (v >= 6)
    {
        for (auto &s : c.layerShorts)
        {
            s = static_cast<uint16_t>(r.u16());
        }
    }
}

// 0x289190
void tailBox(Reader &r, InterfaceComponentDef &c)
{
    c.tailColor = r.u32();
    c.boxFlag = r.flag();
    c.tailAlpha = r.alpha();
}

// 0x28CCC0: the text record, whose colour/alpha become the shared tail.
void tailText(Reader &r, InterfaceComponentDef &c, int v)
{
    readTextInfo(r, c.text, v);
    c.tailColor = c.text.color;
    c.tailAlpha = c.text.alpha;
}

// 0x28B910
void tailSprite(Reader &r, InterfaceComponentDef &c, int v)
{
    readColorState(r, c.spriteState, v);
    c.tailAlpha = c.spriteState.alpha;
    c.tailColor = c.spriteState.color3 & 0xFFFFFFu;
}

// 0x391880
void tailModel(Reader &r, InterfaceComponentDef &c)
{
    c.modelId = r.smart();
    c.modelFlags = r.u8();
    if (c.modelFlags & 1)
    {
        c.modelOffsetX = static_cast<int16_t>(r.u16());
        c.modelOffsetY = static_cast<int16_t>(r.u16());
        c.modelRotateX = r.u16();
        c.modelRotateY = r.u16();
        c.modelRotateZ = r.u16();
        c.modelZoom = r.u16();
    }
    else if (c.modelFlags & 2)
    {
        c.modelOffsetX = static_cast<int16_t>(r.u16());
        c.modelOffsetY = static_cast<int16_t>(r.u16());
        c.modelOriginX = static_cast<int16_t>(r.u16());
        c.modelRotateX = r.u16();
        c.modelRotateY = r.u16();
        c.modelRotateZ = r.u16();
        c.modelZoom = r.u16();
    }
    c.modelAnimId = r.smart();
    // The header stored (widthMode, 1) / (heightMode, 1) at +1012/+1014 for
    // agent type 4, so these two reads are gated on the size modes.
    if (c.widthMode != 0)
    {
        c.modelAnimSecondary = r.u16();
    }
    if (c.heightMode != 0)
    {
        c.modelAnimTertiary = r.u16();
    }
}

// 0x272F70
void tailLine(Reader &r, InterfaceComponentDef &c)
{
    c.lineThickness = static_cast<uint8_t>(r.u8());
    c.tailColor = r.u32();
    c.lineFlag = r.flag();
}

// 0x25F030
void tailButton(Reader &r, InterfaceComponentDef &c, int v)
{
    c.buttonFlag0 = r.flag();
    c.buttonShape = static_cast<uint8_t>(r.u8());
    c.buttonVariant = static_cast<uint8_t>(r.u8());
    c.buttonToggle = r.flag();
    c.buttonClickable = r.flag();
    c.buttonHighlightColor = r.le32();
    readTailColor(r, c);
    readColorState(r, c.buttonSprite, v);
    readTextInfo(r, c.buttonLabel, v);
}

// 0x27CD00
void tailPanel(Reader &r, InterfaceComponentDef &c)
{
    c.panelW = r.u16();
    c.panelH = r.u16();
    c.panelFlag = r.flag();
    c.panelByte = static_cast<uint8_t>(r.u8());
}

// 0x263DA0
void tailDivider(Reader &r, InterfaceComponentDef &c, int v)
{
    c.dividerFlagA = r.flag();
    c.dividerFlagB = r.flag();
    c.dividerByteA = static_cast<uint8_t>(r.u8());
    c.dividerByteB = static_cast<uint8_t>(r.u8());
    readTailColor(r, c);
    readColorState(r, c.dividerSprite, v);
    readTextInfo(r, c.dividerLabel, v);
}

// 0x270BC0
void tailInput(Reader &r, InterfaceComponentDef &c, int v)
{
    c.tailLeadFlag = r.flag();
    c.inputFlagA = static_cast<uint8_t>(r.u8());
    c.inputFlagB = static_cast<uint8_t>(r.u8());
    c.inputMaxLength = r.u16();
    if (v >= 9)
    {
        c.inputCursorColor = r.le32();
    }
    if (v >= 7)
    {
        c.inputExtraByte = static_cast<uint8_t>(r.u8());
    }
    readTailColor(r, c);
    readColorState(r, c.inputSprite, v);
    readTextInfo(r, c.inputLabel, v);
    readHover(r, c.inputHover, v);
    if (c.inputMaxLength == 0)
    {
        c.inputMaxLength = 255;
    }
    else if (c.inputMaxLength >= 2500)
    {
        c.inputMaxLength = 2500;
    }
}

// 0x289A90
void tailSlider(Reader &r, InterfaceComponentDef &c, int v)
{
    c.sliderFlag = r.flag();
    c.sliderTrack = r.u32();
    c.sliderFill = r.u32();
    c.sliderHandle = r.u32();
    c.sliderShadow1 = r.u32();
    c.sliderShadow2 = r.u32();
    c.sliderItems = readStrings(r, r.u16());
    for (auto &s : c.sliderShorts)
    {
        s = static_cast<uint16_t>(r.u16());
    }
    readTailColor(r, c);
    readTextInfo(r, c.sliderLabel, v);
    for (auto &s : c.sliderSprites)
    {
        readColorState(r, s, v);
    }
}

// 0x26DED0
void tailGrid(Reader &r, InterfaceComponentDef &c)
{
    c.gridCellW = r.u16();
    c.gridCellH = r.u16();
    c.gridHGap = static_cast<uint8_t>(r.u8());
    c.gridCols = r.u16();
    c.gridRows = r.u16();
    c.gridFlag = r.flag();
}

// Labels, then an id count that must equal the label count, then the ids.
// Returns false when the counts differ: the client returns from the tail there.
bool readLabelledIds(Reader &r, std::vector<std::pair<std::string, uint32_t>> &items)
{
    std::vector<std::string> labels = readStrings(r, r.u16());
    items.clear();
    for (auto &label : labels)
    {
        items.emplace_back(std::move(label), 0u);
    }
    if (static_cast<size_t>(r.u16()) != items.size())
    {
        return false;
    }
    for (auto &item : items)
    {
        item.second = r.u32();
    }
    return true;
}

// 0x274320
void tailList(Reader &r, InterfaceComponentDef &c, int v)
{
    c.listFlag = r.flag();
    c.listBytes[0] = static_cast<uint8_t>(r.u8());
    c.listBytes[1] = static_cast<uint8_t>(r.u8());
    c.listBytes[2] = static_cast<uint8_t>(r.u8());
    if (v >= 9)
    {
        c.listBytes[3] = static_cast<uint8_t>(r.u8());
    }
    c.listBytes[4] = static_cast<uint8_t>(r.u8());
    c.listBytes[5] = static_cast<uint8_t>(r.u8());
    if (!readLabelledIds(r, c.listItems))
    {
        c.isTailTruncated = true;
        return;
    }
    int groupCount = r.u16();
    for (int i = 0; i < groupCount && !r.isOverrun(); i++)
    {
        c.listGroups.push_back(r.u16());
    }
    if (v >= 9)
    {
        c.listColorA = r.le32();
        c.listColorB = r.le32();
    }
    readTailColor(r, c);
    for (auto &s : c.listSprites)
    {
        readColorState(r, s, v);
    }
    readTextInfo(r, c.listLabel, v);
    readHover(r, c.listHover, v);
}

// 0x268960
void tailCombo(Reader &r, InterfaceComponentDef &c, int v)
{
    c.tailLeadFlag = r.flag();
    for (auto &b : c.comboBytes)
    {
        b = static_cast<uint8_t>(r.u8());
    }
    if (!readLabelledIds(r, c.comboItems))
    {
        c.isTailTruncated = true;
        return;
    }
    c.comboSelectedId = r.u16();
    for (auto &b : c.comboTrailing)
    {
        b = static_cast<uint8_t>(r.u8());
    }
    readTailColor(r, c);
    for (auto &s : c.comboSprites)
    {
        readColorState(r, s, v);
    }
    readTextInfo(r, c.comboLabel, v);
    readHover(r, c.comboHover, v);
}

// 0x27A2F0
void tailPagedLayer(Reader &r, InterfaceComponentDef &c, int v)
{
    c.pageCount = r.u8();
    c.pageBytes[0] = static_cast<uint8_t>(r.u8());
    if (r.u16() != 0)
    {
        c.pageLabels = readStrings(r, c.pageCount);
    }
    if (r.u16() != 0)
    {
        for (int i = 0; i < c.pageCount && !r.isOverrun(); i++)
        {
            c.pageIds.push_back(r.u32());
        }
    }
    c.pageColorA = r.le32();
    readTailColor(r, c);
    readColorState(r, c.pageSprites[0], v);
}

// 0x262670. With labels present the client skips the following u16 and reads
// no id list; with none it reads a u16 count and that many u32s.
void tailCarousel(Reader &r, InterfaceComponentDef &c, int v)
{
    c.pageFlag = r.flag();
    c.pageCount = r.u16();
    if (c.pageCount > 0)
    {
        c.pageLabels = readStrings(r, c.pageCount);
        r.skip(2);
    }
    else
    {
        c.pageCount = r.u16();
        for (int i = 0; i < c.pageCount && !r.isOverrun(); i++)
        {
            c.pageIds.push_back(r.u32());
        }
    }
    if (r.u16() != c.pageCount)
    {
        c.isTailTruncated = true;
        return;
    }
    for (int i = 0; i < c.pageCount && !r.isOverrun(); i++)
    {
        c.pageValues.push_back(r.u32());
    }
    for (auto &s : c.pageShorts)
    {
        s = static_cast<uint16_t>(r.u16());
    }
    c.pageColorA = r.le32();
    readTailColor(r, c);
    readTextInfo(r, c.pageLabel, v);
    readColorState(r, c.pageSprites[0], v);
    readColorState(r, c.pageSprites[1], v);
}

// 0x277CC0
void tailPagedCarousel(Reader &r, InterfaceComponentDef &c, int v)
{
    c.pageFlag = r.flag();
    c.pageCount = r.u8();
    c.pageBytes[0] = static_cast<uint8_t>(r.u8());
    c.pageBytes[1] = static_cast<uint8_t>(r.u8());
    c.pageShorts[0] = static_cast<uint16_t>(r.u16());
    c.pageBytes[2] = static_cast<uint8_t>(r.u8());
    c.pageShorts[1] = static_cast<uint16_t>(r.u16());
    c.pageShorts[2] = static_cast<uint16_t>(r.u16());
    c.pageShorts[3] = static_cast<uint16_t>(r.u16());
    c.pageIntA = r.u32();
    c.pageIntB = r.u32();
    c.pageBytes[3] = static_cast<uint8_t>(r.u8() == 1);
    c.pageColorA = r.le32();
    c.pageColorB = r.le32();
    readTailColor(r, c);
    for (auto &s : c.pageSprites)
    {
        readColorState(r, s, v);
    }
}

// 0x27BB90
void tailComponent19(Reader &r, InterfaceComponentDef &c, int v)
{
    c.c19FlagA = r.flag();
    for (int i = 0; i < 5; i++)
    {
        c.c19Shorts[i] = static_cast<uint16_t>(r.u16());
    }
    c.c19Flag = r.flag();
    c.c19ColorA = r.le32();
    c.c19ColorB = r.le32();
    c.c19Shorts[5] = static_cast<uint16_t>(r.u16());
    readTailColor(r, c);
    for (auto &s : c.c19Sprites)
    {
        readColorState(r, s, v);
    }
    readTextInfo(r, c.c19Label, v);
}

// 0x288580: labels, a skipped u16, one u32 per label.
void tailRadioGroup(Reader &r, InterfaceComponentDef &c, int v)
{
    c.radioFlag = r.flag();
    for (auto &b : c.radioBytes)
    {
        b = static_cast<uint8_t>(r.u8());
    }
    std::vector<std::string> labels = readStrings(r, r.u16());
    r.skip(2);
    for (auto &label : labels)
    {
        uint32_t id = r.u32();
        c.radioItems.emplace_back(std::move(label), id);
    }
    c.radioSelected = r.u8();
    c.radioColorA = r.le32();
    c.radioColorB = r.le32();
    readTailColor(r, c);
    readTextInfo(r, c.radioLabel, v);
    readColorState(r, c.radioSprite, v);
}

// 0x26E840
void tailGroupBox(Reader &r, InterfaceComponentDef &c, int v)
{
    c.groupBoxW = r.u16();
    c.groupBoxH = r.u16();
    c.groupBoxFlag = static_cast<uint8_t>(r.u8());
    for (auto &b : c.groupBoxBytes)
    {
        b = static_cast<uint8_t>(r.u8());
    }
    c.groupBoxColor = r.le32();
    readTailColor(r, c);
    readTextInfo(r, c.groupBoxLabel, v);
    readColorState(r, c.groupBoxSprites[0], v);
    readColorState(r, c.groupBoxSprites[1], v);
}

// 0x27DAE0
void tailRadialProgress(Reader &r, InterfaceComponentDef &c, int v)
{
    readTailColor(r, c);
    readColorState(r, c.radialSprite, v);
}

// 0x260730
void tailCrmView(Reader &r, InterfaceComponentDef &c, int v)
{
    c.crmFlagA = r.flag();
    int intCount = r.u16();
    for (int i = 0; i < intCount && !r.isOverrun(); i++)
    {
        c.crmIntList.push_back(r.u32());
    }
    c.crmString = r.str();
    c.crmFlag = static_cast<uint8_t>(r.u8());
    std::vector<std::string> names = readStrings(r, r.u16());
    int idCount = r.u16();
    for (int i = 0; i < idCount && !r.isOverrun(); i++)
    {
        uint32_t id = r.u32();
        c.crmMap.emplace_back(id, i < static_cast<int>(names.size()) ? names[i] : std::string{});
    }
    if (v >= 11)
    {
        c.crmString1 = r.str();
        c.crmString2 = r.str();
    }
}

// 0x391CD0. The client negates position[1]; the raw value is kept here.
void tailComponent28(Reader &r, InterfaceComponentDef &c)
{
    int count = r.u16();
    for (int i = 0; i < count && !r.isOverrun(); i++)
    {
        InterfaceComponentDef::Component28Entry e;
        e.key = i;
        for (auto &f : e.position)
        {
            f = r.f32();
        }
        e.flagA = static_cast<uint8_t>(r.u8());
        for (auto &f : e.rotation)
        {
            f = r.f32();
        }
        for (auto &f : e.scale)
        {
            f = r.f32();
        }
        e.misc1 = static_cast<float>(r.u16());
        e.misc2 = r.f32();
        e.misc3 = r.f32();
        uint32_t argb = r.u32();
        e.color[0] = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
        e.color[1] = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
        e.color[2] = static_cast<float>(argb & 0xFF) / 255.0f;
        e.color[3] = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
        e.flagB = static_cast<uint8_t>(r.u8());
        c.c28Entries.push_back(e);
    }
    c.c28Trailing = static_cast<uint8_t>(r.u8());
}

// ComponentFactory::CreateComponent (0xBB850): the subtype overrides win
// first, then the factory type.
ComponentDecoder resolveDecoder(int type, int subtype)
{
    switch (subtype)
    {
        case 1405:
            return ComponentDecoder::Text;
        case 1406:
            if (type == 4)
            {
                return ComponentDecoder::Text;
            }
            break;
        case 1339:
            if (type == 5)
            {
                return ComponentDecoder::Sprite;
            }
            break;
        default:
            break;
    }
    switch (type)
    {
        case 0:  return ComponentDecoder::Layer;
        case 3:  return ComponentDecoder::Box;
        case 4:  return ComponentDecoder::Text;
        case 5:  return ComponentDecoder::Sprite;
        case 6:  return ComponentDecoder::Model;
        case 9:  return ComponentDecoder::Line;
        case 10: return ComponentDecoder::Button;
        case 11: return ComponentDecoder::Panel;
        case 12: return ComponentDecoder::Divider;
        case 13: return ComponentDecoder::Input;
        case 14: return ComponentDecoder::Slider;
        case 15: return ComponentDecoder::Grid;
        case 16: return ComponentDecoder::List;
        case 17: return ComponentDecoder::Combo;
        case 18: return ComponentDecoder::PagedLayer;
        case 19: return ComponentDecoder::Component19;
        case 20: return ComponentDecoder::Carousel;
        case 21: return ComponentDecoder::PagedCarousel;
        case 22: return ComponentDecoder::RadioGroup;
        case 23: return ComponentDecoder::GroupBox;
        case 24: return ComponentDecoder::RadialProgress;
        case 26: return ComponentDecoder::CrmView;
        case 27: return ComponentDecoder::Layer;
        case 28: return ComponentDecoder::Component28;
        case 29: return ComponentDecoder::None;
        default: return ComponentDecoder::Unknown;
    }
}

// The vtable +8 getter of the class the factory builds (the agent's category
// byte). Subtype 1405/1406 classes report text (2) whatever the type.
int resolveAgentType(int type, int subtype)
{
    if (subtype == 1405 || (subtype == 1406 && type == 4))
    {
        return 2;
    }
    static constexpr int kAgentType[30] = {
        20, -1, -1,  1,  2,  3,  4, -1, -1,  0,
         9, 21, 10, 12, 13, 22, 11, 14, 23, 15,
        16, 17, 18, 24,  6, -1, 19,  7,  5,  8,
    };
    return (type >= 0 && type < 30) ? kAgentType[type] : -1;
}

// Slot +72 dispatch.
void readTypeTail(Reader &r, InterfaceComponentDef &c)
{
    int v = c.version;
    switch (c.decoder)
    {
        case ComponentDecoder::Layer:          tailLayer(r, c, v); break;
        case ComponentDecoder::Box:            tailBox(r, c); break;
        case ComponentDecoder::Text:           tailText(r, c, v); break;
        case ComponentDecoder::Sprite:         tailSprite(r, c, v); break;
        case ComponentDecoder::Model:          tailModel(r, c); break;
        case ComponentDecoder::Line:           tailLine(r, c); break;
        case ComponentDecoder::Button:         tailButton(r, c, v); break;
        case ComponentDecoder::Panel:          tailPanel(r, c); break;
        case ComponentDecoder::Divider:        tailDivider(r, c, v); break;
        case ComponentDecoder::Input:          tailInput(r, c, v); break;
        case ComponentDecoder::Slider:         tailSlider(r, c, v); break;
        case ComponentDecoder::Grid:           tailGrid(r, c); break;
        case ComponentDecoder::List:           tailList(r, c, v); break;
        case ComponentDecoder::Combo:          tailCombo(r, c, v); break;
        case ComponentDecoder::PagedLayer:     tailPagedLayer(r, c, v); break;
        case ComponentDecoder::Component19:    tailComponent19(r, c, v); break;
        case ComponentDecoder::Carousel:       tailCarousel(r, c, v); break;
        case ComponentDecoder::PagedCarousel:  tailPagedCarousel(r, c, v); break;
        case ComponentDecoder::RadioGroup:     tailRadioGroup(r, c, v); break;
        case ComponentDecoder::GroupBox:       tailGroupBox(r, c, v); break;
        case ComponentDecoder::RadialProgress: tailRadialProgress(r, c, v); break;
        case ComponentDecoder::CrmView:        tailCrmView(r, c, v); break;
        case ComponentDecoder::Component28:    tailComponent28(r, c); break;
        case ComponentDecoder::None:
        case ComponentDecoder::Unknown:
            break;
    }
}

// --------------------------------------------------------------- header ----

// sub_3401F0, up to and including interactFlag.
void readHeaderGeometry(Reader &r, InterfaceComponentDef &c)
{
    c.rawX = r.u16();
    c.rawY = r.u16();
    c.rawWidth = r.u16();
    c.rawHeight = r.u16();
    c.widthMode = r.u8();
    c.heightMode = r.u8();
    c.xMode = r.u8();
    c.yMode = r.u8();
    if (c.widthMode == 4 || c.heightMode == 4)
    {
        c.aspectWidth = r.u16();
        c.aspectHeight = r.u16();
    }
    c.parentScopeId = r.u16();
    c.interactFlag = r.u8();
    c.isHidden = (c.interactFlag & 1) != 0;
}

// sub_26AF80: op base, packed counts (low nibble = ops, high nibble = op
// cursors, of which at most two are stored), ops, cursors, trailing string.
void readOps(Reader &r, InterfaceComponentDef &c)
{
    c.opBase = r.str();
    int packed = r.u8();
    int cursorCount = packed >> 4;
    c.ops = readStrings(r, packed & 0x0F);
    for (int i = 0; i < cursorCount && i < 2; i++)
    {
        ComponentOpCursor cursor;
        cursor.index = r.u8();
        cursor.cursor = r.u16();
        c.opCursors.push_back(cursor);
    }
    c.opSuffix = r.str();
}

void readOptions(Reader &r, InterfaceComponentDef &c)
{
    int packed = r.u8();
    while (packed != 0 && !r.isOverrun())
    {
        ComponentOption opt;
        opt.slot = packed >> 4;
        int ct = ((packed & 0x0F) << 8) | r.u8();
        opt.contentType = (ct == 0xFFF) ? -1 : ct;
        opt.actionByte = r.u8();
        opt.cursorByte = r.u8();
        c.options.push_back(opt);
        packed = r.u8();
    }
}

// sub_26B7A0 (version >= 0 only)
void readEventTables(Reader &r, InterfaceComponentDef &c)
{
    int scriptCount = r.u8();
    for (int i = 0; i < scriptCount && !r.isOverrun(); i++)
    {
        ComponentEventScript e;
        e.eventId = r.medium();
        e.script = r.u32();
        c.eventScripts.push_back(e);
    }
    int stringCount = r.u8();
    for (int i = 0; i < stringCount && !r.isOverrun(); i++)
    {
        ComponentEventString e;
        e.eventId = r.medium();
        e.flag = r.u8();
        if (e.flag == 0)
        {
            e.value = r.str();
        }
        c.eventStrings.push_back(e);
    }
}

// sub_33FE40: count byte; entry 0 = type byte + u32 script id; later entries
// = type byte + payload (0: u32, 1: string, other: nothing).
void readHook(Reader &r, InterfaceComponentDef &c, int hookId)
{
    int count = r.u8();
    if (count == 0)
    {
        return;
    }
    ComponentHook hook;
    hook.hookId = hookId;
    hook.scriptTypeByte = r.u8();
    hook.scriptId = static_cast<int32_t>(r.u32());
    for (int i = 1; i < count && !r.isOverrun(); i++)
    {
        ComponentHookArg arg;
        arg.type = r.u8();
        if (arg.type == 0)
        {
            arg.intValue = static_cast<int32_t>(r.u32());
        }
        else if (arg.type == 1)
        {
            arg.stringValue = r.str();
        }
        hook.args.push_back(std::move(arg));
    }
    c.hooks.push_back(std::move(hook));
}

// sub_33FB80: hooks in the client's read order, then five u32 trigger lists
// (sub_340130: count byte + u32s).
void readHooks(Reader &r, InterfaceComponentDef &c)
{
    static constexpr int kHooksA[] = {35, 4, 6, 16, 15, 17, 18, 19, 36, 37};
    static constexpr int kHooksB[] = {5, 0, 1, 2, 3, 7, 8, 9, 20, 21};
    int v = c.version;
    for (int id : kHooksA)
    {
        readHook(r, c, id);
    }
    if (v >= 0)
    {
        readHook(r, c, 38);
    }
    for (int id : kHooksB)
    {
        readHook(r, c, id);
    }
    if (v >= 6)
    {
        readHook(r, c, 49);
        readHook(r, c, 50);
        readHook(r, c, 51);
    }
    if (v >= 8)
    {
        readHook(r, c, 52);
    }
    for (auto &list : c.triggers)
    {
        int count = r.u8();
        for (int i = 0; i < count && !r.isOverrun(); i++)
        {
            list.push_back(r.u32());
        }
    }
}

// sub_3401F0 after the tail.
void readHeaderTrailer(Reader &r, InterfaceComponentDef &c)
{
    int v = c.version;
    if (v >= 6)
    {
        c.triggerScriptRef = static_cast<int>(r.u32());
    }
    if (v >= 9)
    {
        c.miscByte = r.u8();
    }
    c.eventMask = (v >= 6) ? r.u32() : static_cast<uint32_t>(r.medium());
    readOptions(r, c);
    readOps(r, c);
    c.cursorPrimary = r.u8();
    c.cursorSecondary = r.u8();
    c.cursorTertiary = r.u8();
    c.targetVerb = r.str();
    if (c.eventMask & 0x3F800u)
    {
        int scope = r.u16();
        c.eventScopeId = (scope == 0xFFFF) ? -1 : scope;
        c.eventScopeMin = r.u16();
        c.eventScopeMax = r.u16();
    }
    if (v >= 0)
    {
        int chain = r.u16();
        c.eventChainId = (chain == 0xFFFF) ? -1 : chain;
        readEventTables(r, c);
    }
    readHooks(r, c);
}

}  // namespace

const char *componentTypeName(int componentType, int subtype)
{
    if (subtype == 1405 || (subtype == 1406 && componentType == 4))
    {
        return "text";
    }
    switch (componentType)
    {
        case 0:  return "layer";
        case 3:  return "box";
        case 4:  return "text";
        case 5:  return "sprite";
        case 6:  return "model";
        case 9:  return "line";
        case 10: return "button";
        case 11: return "crm";
        case 12: return "divider";
        case 13: return "input";
        case 14: return "slider";
        case 15: return "grid";
        case 16: return "list";
        case 17: return "combo";
        case 18: return "pagedLayer";
        case 19: return "component19";
        case 20: return "carousel";
        case 21: return "pagedCarousel";
        case 22: return "radioGroup";
        case 23: return "groupBox";
        case 24: return "radialProgress";
        case 26: return "type26";
        case 27: return "cutscene";
        case 28: return "component28";
        case 29: return "component29";
        default: return "unknown";
    }
}

void InterfaceComponentDef::decode(RSBuffer &buffer)
{
    isDecoded = false;
    size_t start = buffer.readPosition;
    fileLength = static_cast<int>(buffer.remaining());
    Reader r(buffer);

    rawVersionByte = r.u8();
    version = (rawVersionByte == 0xFF) ? -1 : rawVersionByte;
    rawTypeByte = r.u8();
    flagsByte = rawTypeByte;
    componentType = rawTypeByte & 0x7F;
    if (rawTypeByte & 0x80)
    {
        debugName = r.str();
    }
    subtype = r.u16();
    decoder = resolveDecoder(componentType, subtype);
    agentType = resolveAgentType(componentType, subtype);

    if (r.isOverrun())
    {
        decodeError = "truncated preamble";
    }
    else if (decoder == ComponentDecoder::Unknown)
    {
        decodeError = "unknown component type " + std::to_string(componentType);
    }
    else
    {
        readHeaderGeometry(r, *this);
        readTypeTail(r, *this);
        readHeaderTrailer(r, *this);
        if (r.isOverrun())
        {
            decodeError = "read past the end of the component";
        }
    }
    bytesConsumed = static_cast<int>(buffer.readPosition - start);
    if (decodeError.empty() && bytesConsumed != fileLength)
    {
        decodeError = "decoded " + std::to_string(bytesConsumed) + " of " +
                      std::to_string(fileLength) + " bytes";
    }
    isDecoded = decodeError.empty();
}
