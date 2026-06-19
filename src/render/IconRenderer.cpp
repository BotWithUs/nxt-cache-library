#include "render/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace nxtrender {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---- Jagex HSL-16 → RGB palette -----------------------------------------
//
// RS model vertex colours (and item op40 recolours) are packed 16-bit Jagex
// HSL: bits [15:10] hue (0..63), [9:7] saturation (0..7), [6:0] luminance
// (0..127). The client precomputes a 65536-entry RGB palette indexed directly
// by the 16-bit value; we reproduce it (brightness 0.8, the common model
// brightness). Verified against item 4151 (abyssal whip): body decodes to dark
// olive/grey, accent 0x03B0 → #C6190C red.

double hue2rgb(double p, double q, double t)
{
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

const std::vector<uint32_t> &hslPalette()
{
    static const std::vector<uint32_t> palette = [] {
        std::vector<uint32_t> p(65536);
        constexpr double brightness = 0.8;
        int off = 0;
        for (int hs = 0; hs < 512; ++hs)
        {
            double hue = static_cast<double>(hs >> 3) / 64.0 + 0.0078125;
            double sat = static_cast<double>(hs & 7) / 8.0 + 0.0625;
            for (int lum = 0; lum < 128; ++lum)
            {
                double light = static_cast<double>(lum) / 128.0;
                double r = light, g = light, b = light;
                if (sat != 0.0)
                {
                    double q = light < 0.5 ? light * (1.0 + sat)
                                           : light + sat - light * sat;
                    double pp = 2.0 * light - q;
                    r = hue2rgb(pp, q, hue + 1.0 / 3.0);
                    g = hue2rgb(pp, q, hue);
                    b = hue2rgb(pp, q, hue - 1.0 / 3.0);
                }
                int ri = static_cast<int>(std::pow(r, brightness) * 256.0);
                int gi = static_cast<int>(std::pow(g, brightness) * 256.0);
                int bi = static_cast<int>(std::pow(b, brightness) * 256.0);
                ri = std::clamp(ri, 0, 255);
                gi = std::clamp(gi, 0, 255);
                bi = std::clamp(bi, 0, 255);
                p[static_cast<size_t>(off++)] =
                    (static_cast<uint32_t>(ri) << 16) |
                    (static_cast<uint32_t>(gi) << 8) | static_cast<uint32_t>(bi);
            }
        }
        return p;
    }();
    return palette;
}

struct Rgb { float r, g, b; };

Rgb hslToRgb(int hsl16)
{
    uint32_t c = hslPalette()[static_cast<size_t>(hsl16 & 0xFFFF)];
    return {static_cast<float>((c >> 16) & 0xFF),
            static_cast<float>((c >> 8) & 0xFF),
            static_cast<float>(c & 0xFF)};
}

// Per-pixel attributes interpolated across a triangle.
struct Vary
{
    float r, g, b, a;
};

struct ScreenVert
{
    float x, y;     // sub-pixel screen position
    float depth;    // camera-space z (smaller = nearer)
    Vary v;
};

// Render one gouraud triangle into the framebuffer with a depth test.
void rasterTriangle(int W, int H, std::vector<float> &zbuf, std::vector<float> &accum,
                    const ScreenVert &a, const ScreenVert &b, const ScreenVert &c)
{
    float minXf = std::min({a.x, b.x, c.x});
    float maxXf = std::max({a.x, b.x, c.x});
    float minYf = std::min({a.y, b.y, c.y});
    float maxYf = std::max({a.y, b.y, c.y});

    int x0 = std::max(0, static_cast<int>(std::floor(minXf)));
    int x1 = std::min(W - 1, static_cast<int>(std::ceil(maxXf)));
    int y0 = std::max(0, static_cast<int>(std::floor(minYf)));
    int y1 = std::min(H - 1, static_cast<int>(std::ceil(maxYf)));
    if (x0 > x1 || y0 > y1) return;

    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-6f) return;
    float invArea = 1.0f / area;

    for (int y = y0; y <= y1; ++y)
    {
        float py = static_cast<float>(y) + 0.5f;
        for (int x = x0; x <= x1; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;
            // Barycentric weights via edge functions.
            float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * invArea;
            float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * invArea;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;  // outside (CW or CCW)

            float depth = w0 * a.depth + w1 * b.depth + w2 * c.depth;
            size_t idx = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
            if (depth >= zbuf[idx]) continue;
            zbuf[idx] = depth;

            float *dst = &accum[idx * 4];
            dst[0] = w0 * a.v.r + w1 * b.v.r + w2 * c.v.r;
            dst[1] = w0 * a.v.g + w1 * b.v.g + w2 * c.v.g;
            dst[2] = w0 * a.v.b + w1 * b.v.b + w2 * c.v.b;
            dst[3] = w0 * a.v.a + w1 * b.v.a + w2 * c.v.a;
        }
    }
}

}  // namespace

RenderedIcon renderModelIcon(const ModelType &model, const IconParams &params,
                             int width, int height)
{
    RenderedIcon out;
    out.width = std::max(0, width);
    out.height = std::max(0, height);
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    if (out.width == 0 || out.height == 0) return out;

    const int vc = model.vertexCount;
    if (vc <= 0 || static_cast<int>(model.vx.size()) < vc) return out;

    const int ss = std::clamp(params.supersample, 1, 8);
    const int W = out.width * ss;
    const int H = out.height * ss;

    // Resize (op110/111/112): 0 means "unset" → 128 (1.0).
    const float sx = (params.resizeX ? params.resizeX : 128) / 128.0f;
    const float sy = (params.resizeY ? params.resizeY : 128) / 128.0f;
    const float sz = (params.resizeZ ? params.resizeZ : 128) / 128.0f;

    // --- Exact r910 software item-sprite transform (com.jagex ObjType) -------
    // The client builds the model→view transform as
    //   v_view = Rx(xan2d) · ( Ry(yan2d) · Rz(-zan2d) · v + T )
    // placing the model on an orbit at distance zoom2d*4 pitched by xan2d, with
    //   T = ( offX*4,  sin(xan2d)*dist - minY/2 + offY*4,  cos(xan2d)*dist + offY*4 )
    // where minY is the model's lowest Y vertex (method14836). Angles are
    // param/2048 of a full turn (param<<3 over a 16384 table).
    auto ang = [](int a) { return static_cast<double>(a) * (2.0 * kPi / 2048.0); };
    const float cZ = std::cos(ang(-params.zan2d)), sZ = std::sin(ang(-params.zan2d));
    const float cY = std::cos(ang(params.yan2d)),  sY = std::sin(ang(params.yan2d));
    const float cX = std::cos(ang(params.xan2d)),  sX = std::sin(ang(params.xan2d));

    const float dist  = static_cast<float>(params.zoom2d) * 4.0f;
    // The cache stores model Y growing downward; the r910 render space is Y-up
    // (models rise into -Y, so method14836/minY ≈ -height). We negate Y per
    // vertex below, so the centring minY here is the negated max.
    const float minYr = -static_cast<float>(model.maxY) * sy;
    const float tx = static_cast<float>(params.offsetX2d) * 4.0f;
    const float ty = sX * dist - minYr * 0.5f + static_cast<float>(params.offsetY2d) * 4.0f;
    const float tz = cX * dist + static_cast<float>(params.offsetY2d) * 4.0f;

    // Native sprite is 36x32 with principal point (16,16) and focal length 512;
    // map that onto the requested (supersampled) WxH target.
    const float mapX = static_cast<float>(W) / 36.0f;
    const float mapY = static_cast<float>(H) / 32.0f;

    // Recolour lookup (op40): HSL-16 source → target.
    auto recolour = [&](int hsl) -> int {
        const size_t n = std::min(params.origColors.size(), params.replColors.size());
        for (size_t i = 0; i < n; ++i)
            if (params.origColors[i] == (hsl & 0xFFFF)) return params.replColors[i];
        return hsl;
    };

    // HSL-space lighting — mirrors the r910 software path (Class401.method8241):
    // the light intensity scales the colour's 7-bit luminance index
    //   lum' = clamp(light * (hsl & 0x7F) >> 7, 2, 126)
    // and is recombined with the unchanged hue+saturation (hsl & 0xFF80), then
    // looked up in the HSL palette. This preserves hue/saturation and only
    // moves brightness — unlike a flat RGB multiply. Light direction is the
    // standard item sun (-50,-10,-50); op113 ambient raises the floor and op114
    // contrast scales the diffuse range (matching create_model's 64+ambient /
    // 768+contrast). 'light' is in method8241 units where 128 ⇒ luminance
    // unchanged.
    const float lx = -50.0f, ly = -10.0f, lz = -50.0f;
    const float lmag = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float ambientLevel = 108.0f + static_cast<float>(params.ambient);
    const float diffuseRange = 48.0f * 768.0f / static_cast<float>(768 + std::max(0, params.contrast));

    const bool haveNormals = static_cast<int>(model.nx.size()) >= vc;
    const bool haveColors = static_cast<int>(model.vertexColors.size()) >= vc;
    const bool haveAlpha = static_cast<int>(model.vertexAlphas.size()) >= vc;

    std::vector<ScreenVert> sv(static_cast<size_t>(vc));

    for (int i = 0; i < vc; ++i)
    {
        const size_t vi = static_cast<size_t>(i);
        float x = model.vx[vi] * sx;
        float y = -model.vy[vi] * sy;   // negate: cache Y-down → render Y-up
        float z = model.vz[vi] * sz;

        // Rz(-zan2d) → Ry(yan2d) → translate → Rx(xan2d)
        float x1 = x * cZ - y * sZ;
        float y1 = x * sZ + y * cZ;
        float x2 = x1 * cY + z * sY;
        float z2 = -x1 * sY + z * cY;
        float x3 = x2 + tx;
        float y3 = y1 + ty;
        float z3 = z2 + tz;
        float y4 = y3 * cX - z3 * sX;
        float z4 = y3 * sX + z3 * cX;
        if (z4 < 1.0f) z4 = 1.0f;

        // Perspective: focal 512, principal point (16,16) in 36x32 sprite space.
        float spX = (512.0f * x3 / z4 + 16.0f) * mapX;
        float spY = (512.0f * y4 / z4 + 16.0f) * mapY;

        // Diffuse term from the model-space normal (Y negated to match vertices).
        float ndotl = 0.5f;
        if (haveNormals)
        {
            float nx = static_cast<float>(model.nx[vi]);
            float ny = -static_cast<float>(model.ny[vi]);   // match negated vertex Y
            float nz = static_cast<float>(model.nz[vi]);
            float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen > 0.0f)
                ndotl = std::max(0.0f, (nx * lx + ny * ly + nz * lz) / (nlen * lmag));
        }
        const int light = static_cast<int>(ambientLevel + diffuseRange * ndotl);

        Rgb base;
        if (haveColors)
        {
            int hsl = recolour(model.vertexColors[vi]) & 0xFFFF;
            int lum = std::clamp((light * (hsl & 0x7F)) >> 7, 2, 126);
            base = hslToRgb((hsl & 0xFF80) | lum);   // hue+sat preserved, lit luminance
        }
        else
        {
            float s = std::clamp(static_cast<float>(light) / 128.0f, 0.0f, 1.6f);
            base = Rgb{160.0f * s, 160.0f * s, 160.0f * s};
        }
        float alpha = haveAlpha ? static_cast<float>(model.vertexAlphas[vi] & 0xFF) : 255.0f;

        sv[vi] = ScreenVert{
            spX, spY, z4,
            Vary{std::clamp(base.r, 0.0f, 255.0f),
                 std::clamp(base.g, 0.0f, 255.0f),
                 std::clamp(base.b, 0.0f, 255.0f),
                 alpha}};
    }

    std::vector<float> zbuf(static_cast<size_t>(W) * H, std::numeric_limits<float>::max());
    std::vector<float> accum(static_cast<size_t>(W) * H * 4, 0.0f);

    for (const auto &render : model.renders)
    {
        const size_t tris = render.indices.size() / 3;
        for (size_t t = 0; t < tris; ++t)
        {
            int ia = render.indices[t * 3 + 0];
            int ib = render.indices[t * 3 + 1];
            int ic = render.indices[t * 3 + 2];
            if (ia < 0 || ia >= vc || ib < 0 || ib >= vc || ic < 0 || ic >= vc) continue;
            const ScreenVert &sa = sv[static_cast<size_t>(ia)];
            const ScreenVert &sb = sv[static_cast<size_t>(ib)];
            const ScreenVert &sc = sv[static_cast<size_t>(ic)];
            // Backface cull by projected winding — drop triangles facing away so
            // the dark interior of open meshes (e.g. a crown) doesn't show
            // through. RS renders single-sided.
            float area = (sb.x - sa.x) * (sc.y - sa.y) - (sb.y - sa.y) * (sc.x - sa.x);
            if (area >= 0.0f) continue;
            rasterTriangle(W, H, zbuf, accum, sa, sb, sc);
        }
    }

    // Box-downsample SSAA → final RGBA. Colour is averaged over covered
    // subpixels only; alpha is coverage-weighted (sum of subpixel alphas / ss²)
    // so edges fade out smoothly against the transparent background.
    const int subpix = ss * ss;
    for (int y = 0; y < out.height; ++y)
    {
        for (int x = 0; x < out.width; ++x)
        {
            float r = 0, g = 0, b = 0, sumA = 0;
            int covered = 0;
            for (int dy = 0; dy < ss; ++dy)
            {
                for (int dx = 0; dx < ss; ++dx)
                {
                    size_t si = (static_cast<size_t>(y * ss + dy) * static_cast<size_t>(W) +
                                 static_cast<size_t>(x * ss + dx)) * 4;
                    float sa = accum[si + 3];
                    if (sa <= 0.0f) continue;
                    ++covered;
                    r += accum[si + 0];
                    g += accum[si + 1];
                    b += accum[si + 2];
                    sumA += sa;
                }
            }
            size_t di = (static_cast<size_t>(y) * static_cast<size_t>(out.width) +
                         static_cast<size_t>(x)) * 4;
            if (covered > 0)
            {
                float invc = 1.0f / static_cast<float>(covered);
                out.rgba[di + 0] = static_cast<uint8_t>(std::clamp(r * invc, 0.0f, 255.0f));
                out.rgba[di + 1] = static_cast<uint8_t>(std::clamp(g * invc, 0.0f, 255.0f));
                out.rgba[di + 2] = static_cast<uint8_t>(std::clamp(b * invc, 0.0f, 255.0f));
                out.rgba[di + 3] = static_cast<uint8_t>(
                    std::clamp(sumA / static_cast<float>(subpix), 0.0f, 255.0f));
            }
        }
    }
    return out;
}

}  // namespace nxtrender
