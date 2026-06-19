#pragma once

#include "core/RSBuffer.h"

#include <cstdint>
#include <vector>

// 3D model decoder for the RS3 "NXT" cache (JS5 index 47, LZMA-wrapped). The
// payload is LITTLE-ENDIAN. Format/field layout follows Skillbert's rsmv schema
// (src/opcodes/models.jsonc). Current RS3 models are version 5 (> 3), which use
// the "meshdata" layout: ONE shared vertex pool (positions/normals/tangents/UVs/
// per-vertex colours+alpha) followed by `meshCount` render submeshes, each with
// a material and a triangle index buffer into that shared pool.
//
// Only the version > 3 layout is decoded (all live RS3 models); the legacy
// per-mesh version <= 3 layout is left undecoded (decode() returns early). The
// decompression layer must yield the decoded model payload before decode() runs.

struct ModelRender
{
    int materialArgument{};       // raw 16-bit material arg (0 = none; → JS5 index 26)
    std::vector<int> indices;     // triangle vertex indices into the shared pool
};

class ModelType
{
public:
    int id;
    int format{};                 // byte 0 (typically 2)
    int version{};                // byte 1 (5 for current RS3)
    int meshCount{};              // number of render submeshes

    // Shared vertex pool.
    int vertexCount{};
    std::vector<int>   vx, vy, vz;       // positions (int16)
    std::vector<int>   nx, ny, nz;       // normals (int8)
    std::vector<float> u, v;             // UVs (decoded from float16)
    std::vector<int>   vertexColors;     // per-vertex 16-bit colour; empty if absent
    std::vector<int>   vertexAlphas;     // per-vertex alpha 0..255; empty if absent

    std::vector<ModelRender> renders;

    bool hasSkin{};
    int totalFaces{};                    // sum of render triangles
    int minX{}, maxX{}, minY{}, maxY{}, minZ{}, maxZ{};

    explicit ModelType() : id(-1) {}

    void decode(RSBuffer &buffer);
};
