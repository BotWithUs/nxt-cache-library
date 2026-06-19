#include "config_types/ModelType.h"

#include <cstring>
#include <limits>

// Little-endian readers (the model payload is LE, unlike the rest of the cache).
// UV half-floats are decoded from their raw 16-bit value. Layout per rsmv
// src/opcodes/models.jsonc (version > 3 "meshdata" path).
namespace {

// groupFlags bits.
constexpr int kHasVertices  = 0x01;
constexpr int kHasFaceBones = 0x04;
constexpr int kHasBoneIds   = 0x08;
constexpr int kHasSkin      = 0x20;

constexpr int kMaxCount = 8'000'000;

int readU8(RSBuffer &b) { return b.readUnsignedByte(); }
int readS8(RSBuffer &b) { return static_cast<signed char>(b.readUnsignedByte()); }

int readU16LE(RSBuffer &b)
{
    int lo = b.readUnsignedByte();
    int hi = b.readUnsignedByte();
    return lo | (hi << 8);
}

int readS16LE(RSBuffer &b)
{
    int v = readU16LE(b);
    return (v & 0x8000) ? (v - 0x10000) : v;
}

int64_t readU32LE(RSBuffer &b)
{
    int64_t b0 = b.readUnsignedByte();
    int64_t b1 = b.readUnsignedByte();
    int64_t b2 = b.readUnsignedByte();
    int64_t b3 = b.readUnsignedByte();
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

float halfToFloat(uint16_t h)
{
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t f;
    if (exp == 0)
    {
        if (mant == 0) { f = sign; }
        else
        {
            exp = 127 - 15 + 1;
            while ((mant & 0x400) == 0) { mant <<= 1; --exp; }
            mant &= 0x3FF;
            f = sign | (exp << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1F) { f = sign | 0x7F800000u | (mant << 13); }
    else { f = sign | ((exp - 15 + 127) << 23) | (mant << 13); }
    float out;
    std::memcpy(&out, &f, sizeof(out));
    return out;
}

// UVs are the one big-endian field: float16 stored high-byte-first.
float readHalfBE(RSBuffer &b)
{
    int hi = b.readUnsignedByte();
    int lo = b.readUnsignedByte();
    return halfToFloat(static_cast<uint16_t>((hi << 8) | lo));
}

}  // namespace

void ModelType::decode(RSBuffer &buffer)
{
    if (buffer.remaining() < 9) return;

    format  = readU8(buffer);
    version = readU8(buffer);
    readU8(buffer);                 // always_0f
    meshCount = readU8(buffer);
    readU8(buffer);                 // unkCount0
    readU8(buffer);                 // unkCount1
    readU8(buffer);                 // unkCount2
    readU8(buffer);                 // unkCount3
    if (version >= 5) readU8(buffer);   // unkCount4

    // Only the modern "meshdata" layout (version > 3) is decoded.
    if (version <= 3 || meshCount < 0 || meshCount > kMaxCount) return;

    const int groupFlags = readU8(buffer);
    const int positionFormat = readU8(buffer);
    readU16LE(buffer);              // meshdata-level faceCount (usually 0)

    const bool hasVertices = (groupFlags & kHasVertices) != 0;
    const bool hasFaceBones = (groupFlags & kHasFaceBones) != 0;
    const bool hasBoneIds = (groupFlags & kHasBoneIds) != 0;
    hasSkin = (groupFlags & kHasSkin) != 0;

    const int64_t vc64 = readU32LE(buffer);
    if (vc64 < 0 || vc64 > kMaxCount) return;
    vertexCount = static_cast<int>(vc64);
    const size_t vc = static_cast<size_t>(vertexCount);

    if (hasVertices)
    {
        vx.resize(vc); vy.resize(vc); vz.resize(vc);
        if (positionFormat == 1)
        {
            // float positions (big-endian) — round to int model space.
            for (size_t i = 0; i < vc; ++i)
            {
                // float_be: 4 bytes big-endian each
                auto rf = [&]() -> float {
                    int b0 = buffer.readUnsignedByte(), b1 = buffer.readUnsignedByte();
                    int b2 = buffer.readUnsignedByte(), b3 = buffer.readUnsignedByte();
                    uint32_t bits = (uint32_t(b0) << 24) | (uint32_t(b1) << 16) |
                                    (uint32_t(b2) << 8) | uint32_t(b3);
                    float f; std::memcpy(&f, &bits, 4); return f;
                };
                vx[i] = static_cast<int>(rf());
                vy[i] = static_cast<int>(rf());
                vz[i] = static_cast<int>(rf());
            }
        }
        else
        {
            for (size_t i = 0; i < vc; ++i)
            {
                vx[i] = readS16LE(buffer);
                vy[i] = readS16LE(buffer);
                vz[i] = readS16LE(buffer);
            }
        }

        nx.resize(vc); ny.resize(vc); nz.resize(vc);
        for (size_t i = 0; i < vc; ++i)
        {
            nx[i] = readS8(buffer);
            ny[i] = readS8(buffer);
            nz[i] = readS8(buffer);
        }

        for (size_t i = 0; i < vc; ++i) buffer.skip(4);   // tangent (short x2)

        u.resize(vc); v.resize(vc);
        for (size_t i = 0; i < vc; ++i)
        {
            u[i] = readHalfBE(buffer);
            v[i] = readHalfBE(buffer);
        }
    }

    if (hasBoneIds)
    {
        for (size_t i = 0; i < vc; ++i) readU16LE(buffer);   // boneid, discard
    }

    if (hasSkin)
    {
        // Per-vertex: ids array (ushort le count + count ushort le),
        //             weights array (ushort le count + count ubyte).
        for (size_t i = 0; i < vc; ++i)
        {
            int idCount = readU16LE(buffer);
            if (idCount < 0 || idCount > kMaxCount) return;
            for (int k = 0; k < idCount; ++k) readU16LE(buffer);
            int wCount = readU16LE(buffer);
            if (wCount < 0 || wCount > kMaxCount) return;
            for (int k = 0; k < wCount; ++k) buffer.readUnsignedByte();
        }
    }

    if (hasVertices)
    {
        vertexColors.resize(vc);
        for (size_t i = 0; i < vc; ++i) vertexColors[i] = readU16LE(buffer);
        vertexAlphas.resize(vc);
        for (size_t i = 0; i < vc; ++i) vertexAlphas[i] = buffer.readUnsignedByte();
    }

    if (hasFaceBones)
    {
        for (size_t i = 0; i < vc; ++i) readU16LE(buffer);   // vertexFacebones, discard
    }

    // Render submeshes: each carries a material + a triangle index buffer.
    renders.reserve(static_cast<size_t>(meshCount));
    for (int r = 0; r < meshCount; ++r)
    {
        if (buffer.remaining() < 1) break;
        ModelRender render;
        readU8(buffer);                       // render groupFlags
        readU32LE(buffer);                    // unkint
        render.materialArgument = readU16LE(buffer);
        readU8(buffer);                       // unkbyte2

        const int idxCount = readU16LE(buffer);
        if (idxCount < 0 || idxCount > kMaxCount) return;
        render.indices.reserve(static_cast<size_t>(idxCount));
        if (vertexCount <= 0xFFFF)
        {
            for (int i = 0; i < idxCount; ++i) render.indices.push_back(readU16LE(buffer));
        }
        else
        {
            for (int i = 0; i < idxCount; ++i) render.indices.push_back(static_cast<int>(readU32LE(buffer)));
        }
        totalFaces += idxCount / 3;
        renders.push_back(std::move(render));
    }

    if (hasVertices && vertexCount > 0)
    {
        int loX = std::numeric_limits<int>::max(), loY = loX, loZ = loX;
        int hiX = std::numeric_limits<int>::min(), hiY = hiX, hiZ = hiX;
        for (size_t i = 0; i < vc; ++i)
        {
            if (vx[i] < loX) loX = vx[i];
            if (vx[i] > hiX) hiX = vx[i];
            if (vy[i] < loY) loY = vy[i];
            if (vy[i] > hiY) hiY = vy[i];
            if (vz[i] < loZ) loZ = vz[i];
            if (vz[i] > hiZ) hiZ = vz[i];
        }
        minX = loX; maxX = hiX; minY = loY; maxY = hiY; minZ = loZ; maxZ = hiZ;
    }
}
