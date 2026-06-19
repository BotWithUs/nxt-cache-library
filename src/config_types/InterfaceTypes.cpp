// Interface component decoder. Mirrors jag::Component::DecodeType + the 22
// per-type tail decoders in rs2client.exe (build 948-5). Buffer layout of one
// component file (one file = one component within the interface archive):
//
//   byte 0  : component type   (0xFF = blank slot)
//   byte 1  : flags            (bit7 = a debug name C-string follows)
//   [name]  : NUL-terminated C-string (when flags & 0x80)
//   2 bytes : subtype          (BE; used by ComponentFactory for super-type 0,3,4 dispatch)
//   <common header>            : decoded by readCommonHeader below
//   <type-specific tail>       : dispatched by readTypeTail
//
// All integer reads are big-endian per existing RSBuffer convention. Reads
// guarded by version branches use the component type byte as the version
// (since RT-NXT interfaces evolved per-type, not via an explicit version field).

#include "config_types/InterfaceTypes.h"

#include <cstdint>

namespace {

// readSmartInt with 0x7FFF-as-negative-one branch (used by Model tail and the
// option-content-type cell). Distinct from RSBuffer::readSmartInt which keeps
// the high bit value.
int readModelSmart(RSBuffer &buf)
{
    if (!buf.canRead(1)) return 0;
    auto peek = static_cast<unsigned char>(buf.buffer[buf.readPosition]);
    if (peek <= 0x7F)
    {
        int v = buf.readUnsignedShort();
        if (v == 0x7FFF) v = -1;
        return v;
    }
    return buf.readInt() & 0x7FFFFFFF;
}

uint32_t readBeU32(RSBuffer &buf)
{
    return static_cast<uint32_t>(buf.readInt());
}

// sub_289D30 — the universal ColorState sub-record. 16 bytes (4 + 2 + 1 +
// 1 + 1 + 4 + 1 + 1 + 4 wait that's 19 — let me recount: u32 + u16 + u8 + u8
// + u8 + u32 + u8 + u8 + u32 = 4+2+1+1+1+4+1+1+4 = 19 bytes legacy; +4 for
// componentType>=6 = 23 bytes).
void readColorState(RSBuffer &buf, ComponentColorState &cs, int componentType)
{
    cs.color1 = readBeU32(buf);
    cs.spritePart = buf.readUnsignedShort();
    unsigned char fb = buf.readUnsignedByte();
    cs.flags = static_cast<uint8_t>((fb & 1) | ((fb & 2) << 2)); // bit0 + bit3
    cs.alpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    cs.priority = buf.readUnsignedByte();
    cs.color2 = readBeU32(buf);
    if (buf.readUnsignedByte() == 1) cs.flags |= 0x02;
    if (buf.readUnsignedByte() == 1) cs.flags |= 0x04;
    cs.color3 = readBeU32(buf);
    if (componentType >= 3)
    {
        if (buf.readUnsignedByte() == 1) cs.flags |= 0x10;
    }
    if (componentType >= 6)
    {
        cs.hasLegacyExtra = true;
        for (int i = 0; i < 4; i++) cs.legacyExtra[i] = buf.readUnsignedByte();
    }
}

// sub_288DA0 — Hover/Action sub-record (4 flag bytes + 3× ColorState).
void readHover(RSBuffer &buf, ComponentHoverInfo &h, int componentType)
{
    if (buf.readUnsignedByte() == 1) h.flags |= 0x01;
    if (buf.readUnsignedByte() == 1) h.flags |= 0x02;
    h.leftX = buf.readUnsignedByte();
    h.leftY = buf.readUnsignedByte();
    for (int i = 0; i < 3; i++) readColorState(buf, h.states[i], componentType);
}

// jag::Component::TextComponent::Decode at 0x28be30 — text sub-record.
void readTextInfo(RSBuffer &buf, ComponentTextInfo &t, int componentType)
{
    t.textId = buf.readSmartInt();
    if (componentType >= 2)
    {
        t.flagBit0 = static_cast<uint8_t>(buf.readUnsignedByte() & 1);
    }
    t.text = buf.readString();
    t.fontSize = buf.readUnsignedByte();
    t.fontStyle = buf.readUnsignedByte();
    t.fontEffect = buf.readUnsignedByte();
    t.flagBit1 = static_cast<uint8_t>(buf.readUnsignedByte() & 1);
    t.color = readBeU32(buf);
    t.alpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    if (componentType < 0) return;
    t.shadow = buf.readUnsignedByte();
}

// sub_262DD0 — event scripts table. Each row consumes 3 bytes (medium-int id)
// + 4 bytes (BE-swap value). Terminated when the count byte read at the top
// of every iteration is 0.
void readEventScripts(RSBuffer &buf, std::vector<ComponentEventScript> &out, int componentType)
{
    if (componentType < 0) return;
    int count = buf.readUnsignedByte();
    if (count == 0) return;
    for (int i = 0; i < count; i++)
    {
        if (buf.remaining() < 7) return;
        ComponentEventScript e;
        e.eventId = buf.readMediumInt();
        e.script  = readBeU32(buf);
        out.push_back(e);
    }
}

// sub_340490(v74, buf, componentType) — additional optional payload at the
// very end of every component. Decompilation suggests it's a no-op for most
// types; we consume nothing here and trust the file boundary to terminate.
void readCommonTrailer(RSBuffer &, int) {}

// Common header (jag::Component::DecodeType). 'componentType' acts as the
// version gate (the engine repurposes the type byte as a version selector for
// the layered RT5/NXT format).
void readCommonHeader(RSBuffer &buf, InterfaceComponentDef &c, int componentType)
{
    c.rawX = buf.readUnsignedShort();
    c.rawY = buf.readUnsignedShort();
    c.rawWidth = buf.readUnsignedShort();
    c.rawHeight = buf.readUnsignedShort();
    c.widthMode = buf.readUnsignedByte();
    c.heightMode = buf.readUnsignedByte();
    c.xMode = buf.readUnsignedByte();
    c.yMode = buf.readUnsignedByte();
    if (c.widthMode == 4 || c.heightMode == 4)
    {
        c.aspectWidth = buf.readUnsignedShort();
        c.aspectHeight = buf.readUnsignedShort();
    }
    c.parentScopeId = buf.readUnsignedShort();

    c.interactFlag = buf.readUnsignedByte();

    if (componentType >= 6)
    {
        int v = buf.readInt();
        c.triggerScriptRef = (v == -1) ? -1 : v;
    }

    if (componentType >= 9)
    {
        c.miscByte = buf.readUnsignedByte();
    }

    if (componentType >= 6)
    {
        c.eventMask = readBeU32(buf);
    }
    else
    {
        c.eventMask = static_cast<uint32_t>(buf.readMediumInt());
    }

    // Option list. Encoded as a packed terminator byte followed by 3 bytes
    // per option (12-bit content type spanning the low nibble of the packed
    // byte + the next byte; then two trailing bytes). A 0 terminator byte
    // ends the list.
    int packed = buf.readUnsignedByte();
    while (packed != 0 && buf.remaining() >= 3)
    {
        ComponentOption opt;
        opt.slot = (packed >> 4);              // 1-based slot index
        int contentLo = buf.readUnsignedByte();
        int ct = ((packed & 0x0F) << 8) | contentLo;
        if (ct == 0xFFF) ct = -1;
        opt.contentType = ct;
        opt.actionByte = buf.readUnsignedByte();
        opt.cursorByte = buf.readUnsignedByte();
        c.options.push_back(opt);
        packed = buf.readUnsignedByte();
    }

    c.cursorPrimary   = buf.readUnsignedByte();
    c.cursorSecondary = buf.readUnsignedByte();
    c.cursorTertiary  = buf.readUnsignedByte();

    if (c.eventMask & 0x3F800u)
    {
        int eid = buf.readUnsignedShort();
        c.eventScopeId = (eid == 0xFFFF) ? -1 : eid;
        c.eventScopeMin = buf.readUnsignedShort();
        c.eventScopeMax = buf.readUnsignedShort();
    }

    if (componentType >= 0)
    {
        int chain = buf.readUnsignedShort();
        c.eventChainId = (chain == 0xFFFF) ? -1 : chain;
    }

    readEventScripts(buf, c.eventScripts, componentType);
    readCommonTrailer(buf, componentType);
}

// ------------------------------------------------------------------ tails ----

void tailSprite(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    readColorState(buf, c.spriteState, ct);
    // The engine mirrors spriteState into the shared tail fields.
    c.tailAlpha = c.spriteState.alpha;
    c.tailColor = c.spriteState.color2;
}

void tailRadialProgress(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    readColorState(buf, c.spriteState, ct);
}

void tailModel(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.modelId = buf.readSmartInt();
    int flags = buf.readUnsignedByte();
    c.modelFlags = flags;
    if (flags & 1)
    {
        c.modelOffsetX = buf.readUnsignedShort();
        c.modelOffsetY = buf.readUnsignedShort();
        c.modelRotateX = buf.readUnsignedShort();
        c.modelRotateY = buf.readUnsignedShort();
        c.modelRotateZ = buf.readUnsignedShort();
        c.modelZoom    = buf.readUnsignedShort();
    }
    else if (flags & 2)
    {
        c.modelOffsetX = buf.readUnsignedShort();
        c.modelOffsetY = buf.readUnsignedShort();
        c.modelOriginX = buf.readUnsignedShort();
        c.modelRotateX = buf.readUnsignedShort();
        c.modelRotateY = buf.readUnsignedShort();
        c.modelRotateZ = buf.readUnsignedShort();
        c.modelZoom    = buf.readUnsignedShort();
    }
    c.modelAnimId = readModelSmart(buf);
    if (flags & 4)
    {
        c.modelAnimSecondary = buf.readUnsignedShort();
    }
    if (flags & 8)
    {
        c.modelAnimTertiary = buf.readUnsignedShort();
    }
    (void) ct;
}

void tailButton(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    // The function-prologue branch in 0x257190 keys off a flag byte and a
    // sub-shape. We replicate just the byte movement so the file is fully
    // consumed; semantics live on the struct's named fields.
    int isType1 = buf.readUnsignedByte();  // passed to sub_257420 in IDA
    int buttonShape = buf.readUnsignedByte();
    int opCount = buf.readUnsignedByte();
    c.buttonToggle   = buf.readUnsignedByte() == 1;
    c.buttonClickable = buf.readUnsignedByte() == 1;
    c.buttonHighlightColor =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    readHover(buf, c.buttonHover, ct);
    readTextInfo(buf, c.buttonLabel, ct);
    (void) isType1; (void) buttonShape; (void) opCount;
}

void tailBox(RSBuffer &buf, InterfaceComponentDef &c, int)
{
    c.tailColor = readBeU32(buf);
    c.boxFlag = buf.readUnsignedByte() == 1;
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
}

void tailLine(RSBuffer &buf, InterfaceComponentDef &c, int)
{
    c.lineThickness = buf.readUnsignedByte();
    c.tailColor = readBeU32(buf);
    c.lineFlag = buf.readUnsignedByte() == 1;
}

void tailPanel(RSBuffer &buf, InterfaceComponentDef &c, int)
{
    c.panelW = buf.readUnsignedShort();
    c.panelH = buf.readUnsignedShort();
    c.panelFlag = buf.readUnsignedByte() == 1;
    c.panelByte = buf.readUnsignedByte();
}

void tailGroupBox(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.groupBoxW = buf.readUnsignedShort();
    c.groupBoxH = buf.readUnsignedShort();
    c.groupBoxFlag = buf.readUnsignedByte();
    for (int i = 0; i < 5; i++) c.groupBoxBytes[i] = buf.readUnsignedByte();
    c.groupBoxColor =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    readTextInfo(buf, c.groupBoxLabel, ct);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
}

void tailGrid(RSBuffer &buf, InterfaceComponentDef &c, int)
{
    c.gridCellW = buf.readUnsignedShort();
    c.gridCellH = buf.readUnsignedShort();
    c.gridHGap = buf.readUnsignedByte();
    c.gridCols = buf.readUnsignedShort();
    c.gridRows = buf.readUnsignedShort();
    c.gridFlag = buf.readUnsignedByte() == 1;
}

void tailCutScene(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.cutSceneW = buf.readUnsignedShort();
    c.cutSceneH = buf.readUnsignedShort();
    if (ct == -1)
    {
        c.cutSceneToggle = buf.readUnsignedByte() != 0;
    }
    else if (ct >= 9)
    {
        c.cutSceneColor =
            static_cast<uint32_t>(buf.readUnsignedByte()) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    }
    else if (ct >= 6)
    {
        for (int i = 0; i < 4; i++) c.cutSceneShorts[i] = buf.readUnsignedShort();
    }
}

void tailCombo(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();   // updates bit0 of interactFlag
    c.combo777 = buf.readUnsignedByte();
    c.combo776 = buf.readUnsignedByte();
    c.combo778 = buf.readUnsignedByte();
    c.combo779 = buf.readUnsignedByte();
    int n = buf.readUnsignedShort();
    c.comboItems.resize(n);
    for (int i = 0; i < n; i++) c.comboItems[i].first = buf.readString();
    int ni = buf.readUnsignedShort();
    if (ni == n)
    {
        for (int i = 0; i < n; i++) c.comboItems[i].second = readBeU32(buf);
    }
    c.comboSelectedId = buf.readUnsignedShort();
    for (int i = 0; i < 8; i++) c.comboBytes[i] = buf.readUnsignedByte();
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readTextInfo(buf, c.comboLabel, ct);
    readHover(buf, c.comboHover, ct);
}

void tailInput(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    c.inputFlagA = buf.readUnsignedByte();
    c.inputFlagB = buf.readUnsignedByte();
    c.inputMaxLength = buf.readUnsignedShort();
    if (ct >= 9)
    {
        c.inputCursorColor =
            static_cast<uint32_t>(buf.readUnsignedByte()) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
        (void) buf.readUnsignedByte();
    }
    else if (ct >= 7)
    {
        (void) buf.readUnsignedByte();
    }
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readTextInfo(buf, c.inputLabel, ct);
    readHover(buf, c.inputHover, ct);
    if (c.inputMaxLength == 0) c.inputMaxLength = 255;
    else if (c.inputMaxLength >= 2500) c.inputMaxLength = 2500;
}

void tailList(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    for (int i = 0; i < 6; i++) c.listBytes[i] = buf.readUnsignedByte();
    if (ct >= 9)
    {
        c.listBytes[6] = buf.readUnsignedByte();
    }
    c.listBytes[7] = buf.readUnsignedByte();  // last "byte0" pair
    int n = buf.readUnsignedShort();
    c.listItems.resize(n);
    for (int i = 0; i < n; i++) c.listItems[i].first = buf.readString();
    int ni = buf.readUnsignedShort();
    if (ni == n)
    {
        for (int i = 0; i < n; i++) c.listItems[i].second = readBeU32(buf);
    }
    int ng = buf.readUnsignedShort();
    c.listGroups.resize(ng);
    for (int i = 0; i < ng; i++) c.listGroups[i] = buf.readUnsignedShort();
    if (ct >= 9)
    {
        c.listColorA =
            static_cast<uint32_t>(buf.readUnsignedByte()) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
        c.listColorB =
            static_cast<uint32_t>(buf.readUnsignedByte()) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
            (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    }
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readTextInfo(buf, c.listLabel, ct);
    readHover(buf, c.listHover, ct);
}

void tailSlider(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    c.sliderTrack = readBeU32(buf);
    c.sliderFill  = readBeU32(buf);
    c.sliderHandle = readBeU32(buf);
    c.sliderShadow1 = readBeU32(buf);
    c.sliderShadow2 = readBeU32(buf);
    int n = buf.readUnsignedShort();
    c.sliderItems.resize(n);
    for (int i = 0; i < n; i++) c.sliderItems[i] = buf.readString();
    for (int i = 0; i < 6; i++) c.sliderShorts[i] = buf.readUnsignedShort();
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    readTextInfo(buf, c.sliderLabel, ct);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
}

void tailCarousel(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    int n = buf.readUnsignedShort();
    c.pageLabels.resize(n);
    for (int i = 0; i < n; i++) c.pageLabels[i] = buf.readString();
    int n2 = buf.readUnsignedShort();
    c.pageIds.resize(n2);
    for (int i = 0; i < n2; i++) c.pageIds[i] = readBeU32(buf);
    int ni = buf.readUnsignedShort();
    (void) ni;
    for (int i = 0; i < n; i++) (void) readBeU32(buf);  // per-item id
    c.pageShortsA[0] = buf.readUnsignedShort();        // +872
    c.pageShortsB[0] = buf.readUnsignedShort();        // +504
    c.pageShortsB[1] = buf.readUnsignedShort();        // +506
    c.pageShortsB[2] = buf.readUnsignedShort();        // +508
    c.pageColorA =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
}

void tailPagedCarousel(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    c.pageBytes[0] = buf.readUnsignedByte();
    c.pageBytes[1] = buf.readUnsignedByte();
    c.pageBytes[2] = buf.readUnsignedByte();
    c.pageBytes[3] = buf.readUnsignedByte();
    c.pageShortsA[0] = buf.readUnsignedShort();
    c.pageBytes[4] = buf.readUnsignedByte();
    c.pageShortsA[1] = buf.readUnsignedShort();
    c.pageShortsA[2] = buf.readUnsignedShort();
    c.pageShortsA[3] = buf.readUnsignedShort();
    (void) readBeU32(buf);
    (void) readBeU32(buf);
    c.pageBytes[5] = buf.readUnsignedByte();
    c.pageColorA =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.pageColorB =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
}

void tailPagedLayer(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.pageBytes[0] = buf.readUnsignedByte();
    c.pageBytes[1] = buf.readUnsignedByte();
    c.pageBytes[2] = buf.readUnsignedByte();
    int hasLabels = buf.readUnsignedShort();
    if (hasLabels)
    {
        // engine sizes labels off an existing page vector; we read N strings
        // until the buffer would be exhausted or hits another non-string byte.
        // Cap at a reasonable upper bound.
        int n = hasLabels;
        for (int i = 0; i < n && buf.remaining() > 0; i++)
        {
            c.pageLabels.push_back(buf.readString());
        }
    }
    int hasIds = buf.readUnsignedShort();
    if (hasIds)
    {
        int n = hasIds;
        for (int i = 0; i < n && buf.remaining() >= 4; i++)
        {
            c.pageIds.push_back(readBeU32(buf));
        }
    }
    c.pageColorA =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
}

void tailComponent19(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    for (int i = 0; i < 5; i++) c.c19Shorts[i] = buf.readUnsignedShort();
    c.c19Flag = buf.readUnsignedByte();
    c.c19ColorA =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.c19ColorB =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.c19Shorts[5] = buf.readUnsignedShort();
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    readColorState(buf, dummy, ct);
    ComponentTextInfo t;
    readTextInfo(buf, t, ct);
}

void tailRadioGroup(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    for (int i = 0; i < 6; i++) c.radioBytes[i] = buf.readUnsignedByte();
    int n = buf.readUnsignedShort();
    c.radioItems.resize(n);
    for (int i = 0; i < n; i++) c.radioItems[i].first = buf.readString();
    (void) buf.readUnsignedShort(); // padding short
    for (int i = 0; i < n; i++) c.radioItems[i].second = readBeU32(buf);
    c.radioSelected = buf.readUnsignedByte();
    c.radioColorA =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.radioColorB =
        static_cast<uint32_t>(buf.readUnsignedByte()) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 8) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 16) |
        (static_cast<uint32_t>(buf.readUnsignedByte()) << 24);
    c.tailAlpha = static_cast<uint8_t>(~buf.readUnsignedByte());
    c.tailColor = readBeU32(buf);
    ComponentTextInfo t;
    readTextInfo(buf, t, ct);
    ComponentColorState dummy;
    readColorState(buf, dummy, ct);
}

void tailCRMView(RSBuffer &buf, InterfaceComponentDef &c, int ct)
{
    c.interactFlag |= buf.readUnsignedByte();
    int n = buf.readUnsignedShort();
    c.crmIntList.resize(n);
    for (int i = 0; i < n; i++) c.crmIntList[i] = readBeU32(buf);
    c.crmString = buf.readString();
    c.crmFlag = buf.readUnsignedByte();
    int n2 = buf.readUnsignedShort();
    std::vector<std::string> tmp;
    tmp.resize(n2);
    for (int i = 0; i < n2; i++) tmp[i] = buf.readString();
    int n3 = buf.readUnsignedShort();
    for (int i = 0; i < n3 && buf.remaining() >= 4 && i < static_cast<int>(tmp.size()); i++)
    {
        uint32_t id = readBeU32(buf);
        c.crmMap[id] = tmp[i];
    }
    if (ct >= 11)
    {
        c.crmString1 = buf.readString();
        c.crmString2 = buf.readString();
    }
}

void tailComponent28(RSBuffer &buf, InterfaceComponentDef &c, int)
{
    int n = buf.readUnsignedShort();
    c.c28Entries.resize(n);
    for (int i = 0; i < n; i++)
    {
        auto &e = c.c28Entries[i];
        e.key = i;
        for (int k = 0; k < 3; k++) e.position[k] = buf.readFloat();
        e.flagA = buf.readUnsignedByte();
        for (int k = 0; k < 3; k++) e.rotation[k] = buf.readFloat();
        for (int k = 0; k < 3; k++) e.scale[k] = buf.readFloat();
        uint16_t s = buf.readUnsignedShort();
        e.misc1 = static_cast<float>(s);
        e.misc2 = buf.readFloat();
        e.misc3 = buf.readFloat();
        uint32_t argb = readBeU32(buf);
        e.color[0] = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
        e.color[1] = static_cast<float>((argb >> 8)  & 0xFF) / 255.0f;
        e.color[2] = static_cast<float>((argb >> 0)  & 0xFF) / 255.0f;
        e.color[3] = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
        e.flagB = buf.readUnsignedByte();
    }
    c.c28Trailing = buf.readUnsignedByte();
}

void readTypeTail(RSBuffer &buf, InterfaceComponentDef &c)
{
    int ct = c.componentType;
    switch (ct)
    {
        case 5:
        case 9:
        case 16:
        case 17:
        case 27:
            tailSprite(buf, c, ct);
            break;
        case 6:
            tailModel(buf, c, ct);
            break;
        case 10:
            tailButton(buf, c, ct);
            break;
        case 11:
        case 15:
            tailList(buf, c, ct);
            break;
        case 12:
            tailCombo(buf, c, ct);
            break;
        case 13:
            tailInput(buf, c, ct);
            break;
        case 14:
            tailSlider(buf, c, ct);
            break;
        case 18:
            tailPagedLayer(buf, c, ct);
            break;
        case 19:
            tailComponent19(buf, c, ct);
            break;
        case 20:
            tailCarousel(buf, c, ct);
            break;
        case 21:
            tailPagedCarousel(buf, c, ct);
            break;
        case 22:
            tailRadioGroup(buf, c, ct);
            break;
        case 23:
            tailGroupBox(buf, c, ct);
            break;
        case 24:
            tailRadialProgress(buf, c, ct);
            break;
        case 26:
            tailCRMView(buf, c, ct);
            break;
        case 28:
            tailComponent28(buf, c, ct);
            break;
        // Subtype-driven specials (Box/Line/Panel/Grid/CutScene) keyed by the
        // 16-bit subtype field rather than the type byte.
        default:
            if (ct == 0 || ct == 3 || ct == 4)
            {
                switch (c.subtype)
                {
                    case 1337: case 1403: tailBox(buf, c, ct); break;
                    case 1338:           tailCutScene(buf, c, ct); break;
                    case 1400:           tailGrid(buf, c, ct); break;
                    case 1401:           tailPanel(buf, c, ct); break;
                    case 1405:           tailLine(buf, c, ct); break;
                    default: break;  // generic layer — no tail
                }
            }
            break;
    }
}

}  // namespace

void InterfaceComponentDef::decode(RSBuffer &buffer)
{
    if (buffer.remaining() == 0) return;

    int typeByte = buffer.readUnsignedByte();
    rawTypeByte = typeByte;
    componentType = (typeByte == 0xFF) ? -1 : typeByte;
    flagsByte = buffer.readUnsignedByte();
    if (flagsByte & 0x80)
    {
        debugName = buffer.readString();
    }
    subtype = buffer.readUnsignedShort();

    if (componentType == -1)
    {
        // Blank slot — no header, no tail.
        return;
    }

    readCommonHeader(buffer, *this, componentType);
    if (buffer.remaining() == 0) return;
    readTypeTail(buffer, *this);
}
