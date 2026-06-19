#pragma once

#include "config_types/ModelType.h"

#include <cstdint>
#include <vector>

// Software renderer that turns an item's inventory model into an RGBA icon
// sprite, approximating how the NXT client produces container/inventory icons.
//
// Fidelity note: the real client renders these on the GPU (a deferred
// G-buffer + SSAO + PBR resolve pass; see rs2client sub_C3220/sub_C5420). That
// pipeline cannot be reproduced byte-for-byte on the CPU, so this renderer
// targets a *recognisable* icon — correct model, orientation (xan/yan/zan),
// per-vertex Jagex-HSL colours + item recolours, and approximate diffuse
// lighting — rather than a pixel-exact match. Framing is auto-fit to the icon
// (the client's exact camera/zoom mapping is GPU-internal), with rotation and
// perspective from the item's 2D parameters applied faithfully.

namespace nxtrender {

// 2D render parameters, taken straight from the item definition (ObjType).
// Angles are in the RS 0..2047 system. resize* of 0 means "unset" (→ 128 = 1x).
struct IconParams
{
    int zoom2d = 2000;                 // op4   camera distance (perspective only)
    int xan2d = 0, yan2d = 0, zan2d = 0;  // op5/op6/op95 rotation (pitch/yaw/roll)
    int offsetX2d = 0, offsetY2d = 0;  // op7/op8 model-space nudge
    int resizeX = 0, resizeY = 0, resizeZ = 0;  // op110/111/112 (0 → 128)
    int ambient = 0;                   // op113 raw signed byte
    int contrast = 0;                  // op114 (already ×5 at decode)
    std::vector<uint16_t> origColors;  // op40 HSL-16 recolour source
    std::vector<uint16_t> replColors;  // op40 HSL-16 recolour target
    int supersample = 4;               // SSAA factor (1 = off, clamped 1..8)
};

struct RenderedIcon
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // width*height*4, row-major, R,G,B,A (A=0 → empty)
};

// Render an already-decoded model into an RGBA icon of the requested size.
// A model with no geometry yields an all-transparent icon (never throws).
RenderedIcon renderModelIcon(const ModelType &model, const IconParams &params,
                             int width, int height);

}  // namespace nxtrender
