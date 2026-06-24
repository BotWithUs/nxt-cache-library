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

    // Trailer: last 2 bytes. The high bit (0x8000) flags the RS3 truecolor format
    // (a single full-colour image, e.g. world-map tiles / login art); otherwise the
    // value is the frame count of a classic palette-indexed group.
    buffer.readPosition = len - 2;
    const int trailer = buffer.readUnsignedShort();
    if ((trailer & 0x8000) != 0)
    {
        decodeTrueColor(buffer, len);
        return;
    }

    const int count = trailer;
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

    // Sanity-cap the total pixel budget before any pixel allocation. A corrupt or
    // non-sprite archive can decode `count`/width/height to implausible values; a
    // single frame at the u16 maximum would demand ~17 GB of RGBA, and the dumper
    // (metadata-only) has already read everything it needs above. Bail out keeping
    // the frame metadata so callers get empty pixels instead of an OOM crash. Real
    // sprite groups are tiny sub-images and stay well under this 64 M-pixel ceiling.
    constexpr long long kMaxSpritePixels = 64LL * 1024 * 1024;   // 64 M px = 256 MB RGBA
    long long totalPixels = 0;
    for (const auto &f : frames)
    {
        totalPixels += static_cast<long long>(f.width) * static_cast<long long>(f.height);
    }
    if (totalPixels > kMaxSpritePixels)
    {
        return;
    }

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

// RS3 truecolor sprite (trailer high-bit set). Front-loaded header, no palette:
//   u16 alphaFlag (0 = RGB 3 bpp, 1 = RGBA 4 bpp), u16 width, u16 height,
//   width*height*(3|4) pixel bytes (R,G,B[,A]), then the u16 0x8001 trailer.
// The size check doubles as the allocation guard: width*height is bounded by the
// actual file length, so a misdetected file can never demand more than it holds.
void SpriteType::decodeTrueColor(RSBuffer &buffer, unsigned int len)
{
    buffer.readPosition = 0;
    const int alphaFlag = buffer.readUnsignedShort();
    const int w = buffer.readUnsignedShort();
    const int h = buffer.readUnsignedShort();
    const bool hasAlpha = (alphaFlag != 0);
    const int bpp = hasAlpha ? 4 : 3;

    const long long need = 6LL + static_cast<long long>(w) * static_cast<long long>(h) * bpp + 2;
    if (w <= 0 || h <= 0 || need > static_cast<long long>(len))
    {
        return;
    }

    canvasWidth = w;
    canvasHeight = h;
    paletteCount = 0;
    frames.assign(1, SpriteFrame{});
    SpriteFrame &f = frames[0];
    f.width = w;
    f.height = h;
    f.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

    const int pixels = w * h;
    for (int j = 0; j < pixels; ++j)
    {
        uint8_t *px = &f.rgba[static_cast<size_t>(j) * 4];
        px[0] = static_cast<uint8_t>(buffer.readUnsignedByte());
        px[1] = static_cast<uint8_t>(buffer.readUnsignedByte());
        px[2] = static_cast<uint8_t>(buffer.readUnsignedByte());
        px[3] = hasAlpha ? static_cast<uint8_t>(buffer.readUnsignedByte()) : 0xFF;
    }
}
