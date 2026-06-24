#pragma once

#include "core/RSBuffer.h"

#include <cstdint>
#include <vector>

// Sprite group decoder for JS5 index 8. One archive = one sprite "group" whose
// single file holds N frames packed in the classic RS dat2 sprite format
// (trailer-addressed: count + per-frame offsets/dimensions + shared palette,
// then per-frame palette-index runs with an optional alpha plane). Frames are
// expanded to RGBA8888 (R,G,B,A byte order, row-major, top-left origin); palette
// index 0 is transparent.

struct SpriteFrame
{
    int offsetX{};   // placement of this frame within the group canvas
    int offsetY{};
    int width{};     // this frame's own pixel dimensions (sub-image)
    int height{};
    std::vector<uint8_t> rgba;   // width*height*4, R,G,B,A
};

class SpriteType
{
public:
    int id;
    int canvasWidth{};    // group canvas (max) dimensions
    int canvasHeight{};
    int paletteCount{};   // including the implicit transparent index 0
    std::vector<SpriteFrame> frames;

    explicit SpriteType() : id(-1) {}

    void decode(RSBuffer &buffer);

private:
    // RS3 truecolor variant (trailer high-bit set): a single full-colour image
    // (RGB or RGBA) with a front-loaded header, not a palette-indexed group.
    void decodeTrueColor(RSBuffer &buffer, unsigned int len);
};
