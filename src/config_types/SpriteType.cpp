#include "config_types/SpriteType.h"

// RS dat2 sprite format (matches RuneLite's SpriteLoader). The buffer is the
// whole decompressed group file; layout is read back-to-front:
//   [pixel data ...][palette RGB][canvas w/h + paletteLen][per-frame arrays][count]
namespace {
constexpr int kFlagVertical = 0x1;   // pixels stored column-major
constexpr int kFlagAlpha    = 0x2;   // an explicit alpha plane follows the indices
}

void SpriteType::decode(RSBuffer &buffer)
{
    const unsigned int len = buffer.writePosition;
    if (len < 2) return;

    // Trailer: last 2 bytes hold the frame count.
    buffer.readPosition = len - 2;
    const int count = buffer.readUnsignedShort();
    if (count <= 0) return;

    // Header block sits 7 + count*8 bytes from the end.
    const long headerPos = static_cast<long>(len) - 7 - static_cast<long>(count) * 8;
    if (headerPos < 0) return;
    buffer.readPosition = static_cast<unsigned int>(headerPos);
    canvasWidth  = buffer.readUnsignedShort();
    canvasHeight = buffer.readUnsignedShort();
    paletteCount = buffer.readUnsignedByte() + 1;   // +1 for the implicit transparent slot

    frames.assign(static_cast<size_t>(count), SpriteFrame{});
    for (auto &f : frames) f.offsetX = buffer.readUnsignedShort();
    for (auto &f : frames) f.offsetY = buffer.readUnsignedShort();
    for (auto &f : frames) f.width   = buffer.readUnsignedShort();
    for (auto &f : frames) f.height  = buffer.readUnsignedShort();

    // Palette (3-byte RGB each) precedes the header block; index 0 is transparent.
    const long palettePos = headerPos - static_cast<long>(paletteCount - 1) * 3;
    if (palettePos < 0) return;
    buffer.readPosition = static_cast<unsigned int>(palettePos);
    std::vector<int> palette(static_cast<size_t>(paletteCount), 0);
    for (int i = 1; i < paletteCount; ++i)
    {
        int rgb = buffer.readMediumInt();
        if (rgb == 0) rgb = 1;   // keep real black distinct from the transparent index
        palette[static_cast<size_t>(i)] = rgb;
    }

    // Pixel data streams from the front, one frame at a time.
    buffer.readPosition = 0;
    for (auto &f : frames)
    {
        const int w = f.width, h = f.height;
        const int dim = w * h;
        if (dim <= 0) continue;

        std::vector<unsigned char> idx(static_cast<size_t>(dim), 0);
        std::vector<unsigned char> alpha(static_cast<size_t>(dim), 0);

        const int flags = buffer.readUnsignedByte();
        const bool vertical = (flags & kFlagVertical) != 0;
        const bool hasAlpha = (flags & kFlagAlpha) != 0;

        if (!vertical)
        {
            for (int j = 0; j < dim; ++j) idx[static_cast<size_t>(j)] = buffer.readUnsignedByte();
        }
        else
        {
            for (int x = 0; x < w; ++x)
                for (int y = 0; y < h; ++y)
                    idx[static_cast<size_t>(w * y + x)] = buffer.readUnsignedByte();
        }

        if (!hasAlpha)
        {
            for (int j = 0; j < dim; ++j)
                alpha[static_cast<size_t>(j)] = idx[static_cast<size_t>(j)] != 0 ? 0xFF : 0x00;
        }
        else if (!vertical)
        {
            for (int j = 0; j < dim; ++j) alpha[static_cast<size_t>(j)] = buffer.readUnsignedByte();
        }
        else
        {
            for (int x = 0; x < w; ++x)
                for (int y = 0; y < h; ++y)
                    alpha[static_cast<size_t>(w * y + x)] = buffer.readUnsignedByte();
        }

        f.rgba.resize(static_cast<size_t>(dim) * 4);
        for (int j = 0; j < dim; ++j)
        {
            const int pi = idx[static_cast<size_t>(j)];
            const int rgb = (pi >= 0 && pi < paletteCount) ? palette[static_cast<size_t>(pi)] : 0;
            uint8_t *px = &f.rgba[static_cast<size_t>(j) * 4];
            px[0] = static_cast<uint8_t>((rgb >> 16) & 0xFF);
            px[1] = static_cast<uint8_t>((rgb >> 8) & 0xFF);
            px[2] = static_cast<uint8_t>(rgb & 0xFF);
            px[3] = alpha[static_cast<size_t>(j)];
        }
    }
}
