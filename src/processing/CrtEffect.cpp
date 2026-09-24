#include "processing/CrtEffect.h"

#include "core/ColorSpace.h"
#include "core/ParallelFor.h"
#include "core/PixelOps.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace kolorit {

using colorspace::clampByte;

namespace {

constexpr float kBezelR = 12.f, kBezelG = 12.f, kBezelB = 15.f;

inline float smoothstepf(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/// Bilinear sample of one channel (0 R, 1 G, 2 B), clamped at the edges
inline float sampleChannel(const QImage& image, float fx, float fy, int channel)
{
    const int w = image.width(), h = image.height();
    fx = std::clamp(fx, 0.f, float(w - 1));
    fy = std::clamp(fy, 0.f, float(h - 1));
    const int x0 = int(fx), y0 = int(fy);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float tx = fx - float(x0), ty = fy - float(y0);
    const QRgb* row0 = constRow(image, y0);
    const QRgb* row1 = constRow(image, y1);
    auto ch = [channel](QRgb c) { return float(channel == 0 ? qRed(c) : channel == 1 ? qGreen(c) : qBlue(c)); };
    const float top = ch(row0[x0]) + (ch(row0[x1]) - ch(row0[x0])) * tx;
    const float bottom = ch(row1[x0]) + (ch(row1[x1]) - ch(row1[x0])) * tx;
    return top + (bottom - top) * ty;
}

/// Screen geometry: source coordinates and coverage (1 inside the screen, 0 on the bezel)
struct Screen {
    float halfW, halfH, k, edge;

    /// Source coordinates in [-1, 1] for the pixel (x, y) and the screen coverage
    void map(int x, int y, float& us, float& vs, float& coverage) const
    {
        const float u = (float(x) - halfW + 0.5f) / halfW;
        const float v = (float(y) - halfH + 0.5f) / halfH;
        // Barrel bulge: the center stays put, the edges go beyond the screen
        const float warp = (1.f + k * (u * u + v * v)) / (1.f + k);
        us = u * warp;
        vs = v * warp;
        // Rounded corners: a superellipse of degree 10
        const float au = std::fabs(us), av = std::fabs(vs);
        const float shape = std::pow(std::pow(au, 10.f) + std::pow(av, 10.f), 0.1f);
        coverage = 1.f - smoothstepf(1.f, 1.f + edge, shape);
    }
};

} // namespace

QString crtMaskName(CrtMask mask)
{
    switch (mask) {
    case CrtMask::None:           return QObject::tr("No mask");
    case CrtMask::ApertureGrille: return QObject::tr("Aperture grille (Trinitron)");
    case CrtMask::ShadowMask:     return QObject::tr("Shadow mask");
    case CrtMask::SlotMask:       return QObject::tr("Slot mask");
    }
    return {};
}

QList<CrtMask> allCrtMasks()
{
    return {CrtMask::None, CrtMask::ApertureGrille, CrtMask::ShadowMask, CrtMask::SlotMask};
}

ImageOp crtOp(const CrtParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        if (in.isNull())
            return {};
        const int width = in.width();
        const int height = in.height();

        const Screen screen{width / 2.f, height / 2.f, float(std::clamp(p.curvature, 0.0, 1.0)) * 0.35f,
                            2.5f / std::max(width, height) * 2.f};
        const float aberration = float(std::max(0, p.aberration));

        // ---------- 1. Curved screen with bezel and aberration ----------
        QImage warped(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(warped);
            parallelRows(height, promise, {0, 35}, [&](int y) {
                QRgb* d = writer.row(y);
                for (int x = 0; x < width; ++x) {
                    float us, vs, cov;
                    screen.map(x, y, us, vs, cov);
                    if (cov <= 0.f) {
                        d[x] = qRgb(int(kBezelR), int(kBezelG), int(kBezelB));
                        continue;
                    }
                    const float sx = (us + 1.f) * 0.5f * (width - 1);
                    const float sy = (vs + 1.f) * 0.5f * (height - 1);
                    // Red and blue drift apart horizontally, as on a badly converged tube
                    const float r = sampleChannel(in, sx - aberration, sy, 0);
                    const float g = sampleChannel(in, sx, sy, 1);
                    const float b = sampleChannel(in, sx + aberration, sy, 2);
                    d[x] = qRgb(clampByte(r * cov + kBezelR * (1.f - cov)), clampByte(g * cov + kBezelG * (1.f - cov)),
                                clampByte(b * cov + kBezelB * (1.f - cov)));
                }
            });
        }
        if (promise.isCanceled())
            return {};

        // ---------- 2. Phosphor glow: bright areas are blurred and added back ----------
        const int radius = std::max(0, p.bloomRadius);
        const float bloom = float(std::clamp(p.bloomStrength, 0.0, 1.0));
        std::vector<float> glow;   // blurred excess brightness, 3 channels per pixel
        if (radius > 0 && bloom > 0.f) {
            std::vector<float> horizontal(size_t(width) * size_t(height) * 3);
            glow.resize(horizontal.size());
            constexpr float threshold = 140.f;

            // Horizontal pass over prefix sums of the excess above the threshold
            parallelRows(height, promise, {35, 50}, [&](int y) {
                const QRgb* s = constRow(warped, y);
                std::vector<float> prefix(size_t(width + 1) * 3, 0.f);
                for (int x = 0; x < width; ++x) {
                    const size_t i = size_t(x) * 3, j = size_t(x + 1) * 3;
                    prefix[j] = prefix[i] + std::max(0.f, qRed(s[x]) - threshold);
                    prefix[j + 1] = prefix[i + 1] + std::max(0.f, qGreen(s[x]) - threshold);
                    prefix[j + 2] = prefix[i + 2] + std::max(0.f, qBlue(s[x]) - threshold);
                }
                float* out = &horizontal[size_t(y) * size_t(width) * 3];
                for (int x = 0; x < width; ++x) {
                    const int lo = std::max(0, x - radius), hi = std::min(width - 1, x + radius);
                    const float inv = 1.f / float(hi - lo + 1);
                    for (int c = 0; c < 3; ++c)
                        out[size_t(x) * 3 + size_t(c)] = (prefix[size_t(hi + 1) * 3 + size_t(c)] - prefix[size_t(lo) * 3 + size_t(c)]) * inv;
                }
            });
            if (promise.isCanceled())
                return {};

            // Vertical pass: every output row sums the neighboring rows
            parallelRows(height, promise, {50, 65}, [&](int y) {
                const int lo = std::max(0, y - radius), hi = std::min(height - 1, y + radius);
                const float inv = 1.f / float(hi - lo + 1);
                float* out = &glow[size_t(y) * size_t(width) * 3];
                std::fill(out, out + size_t(width) * 3, 0.f);
                for (int yy = lo; yy <= hi; ++yy) {
                    const float* row = &horizontal[size_t(yy) * size_t(width) * 3];
                    for (size_t i = 0; i < size_t(width) * 3; ++i)
                        out[i] += row[i];
                }
                for (size_t i = 0; i < size_t(width) * 3; ++i)
                    out[i] *= inv;
            });
            if (promise.isCanceled())
                return {};
        }

        // ---------- 3. Mask, scanlines, vignette, glass reflection, gain ----------
        QImage out(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(out);
            const int maskPeriod = std::max(2, p.maskPeriod);
            const float maskStrength = float(std::clamp(p.maskStrength, 0.0, 1.0));
            const int scanPeriod = std::max(2, p.scanlinePeriod);
            const float scanIntensity = float(std::clamp(p.scanlineIntensity, 0.0, 1.0));
            const float vignette = float(std::clamp(p.vignette, 0.0, 1.0));
            const float glass = float(std::clamp(p.glass, 0.0, 1.0));
            const float gain = float(std::clamp(p.gain, 0.5, 2.0));
            constexpr float twoPi = 2.f * std::numbers::pi_v<float>;
            const float invDiag = 1.f / std::sqrt(2.f);

            parallelRows(height, promise, {65, 100}, [&](int y) {
                const QRgb* s = constRow(warped, y);
                QRgb* d = writer.row(y);
                const float* g = glow.empty() ? nullptr : &glow[size_t(y) * size_t(width) * 3];
                const float scanMul = 1.f - scanIntensity * 0.5f * (1.f + std::cos(twoPi * float(y) / float(scanPeriod)));
                // Triad offset every other row for the shadow mask, and gaps for the slot mask
                const int rowGroup = y / maskPeriod;
                const int stagger = (rowGroup & 1) ? maskPeriod / 2 : 0;
                const float v = (float(y) - screen.halfH) / screen.halfH;

                for (int x = 0; x < width; ++x) {
                    float us, vs, cov;
                    screen.map(x, y, us, vs, cov);
                    if (cov <= 0.f) {
                        d[x] = s[x];
                        continue;
                    }
                    float r = float(qRed(s[x])), gg = float(qGreen(s[x])), b = float(qBlue(s[x]));
                    if (g) {
                        r += g[size_t(x) * 3] * bloom * 1.6f;
                        gg += g[size_t(x) * 3 + 1] * bloom * 1.6f;
                        b += g[size_t(x) * 3 + 2] * bloom * 1.6f;
                    }

                    // Phosphor mask: in every triad one subpixel glows fully, the others are dimmed
                    if (p.mask != CrtMask::None && maskStrength > 0.f) {
                        const int phaseX = p.mask == CrtMask::ShadowMask ? (x + stagger) % maskPeriod : x % maskPeriod;
                        const int sub = phaseX * 3 / maskPeriod;   // 0 — R, 1 — G, 2 — B
                        const float dim = 1.f - maskStrength;
                        if (sub != 0) r *= dim;
                        if (sub != 1) gg *= dim;
                        if (sub != 2) b *= dim;
                        if (p.mask == CrtMask::SlotMask || p.mask == CrtMask::ShadowMask) {
                            // Horizontal gap between triads, in a checkerboard pattern
                            const int slotPhase = (y + ((x / maskPeriod) & 1 ? maskPeriod : 0)) % (maskPeriod * 2);
                            if (slotPhase == 0) {
                                r *= dim;
                                gg *= dim;
                                b *= dim;
                            }
                        }
                    }

                    float mul = scanMul * gain;
                    if (vignette > 0.f) {
                        const float u = (float(x) - screen.halfW) / screen.halfW;
                        const float dist = std::sqrt(u * u + v * v) * invDiag;
                        mul *= 1.f - vignette * smoothstepf(0.3f, 1.f, dist);
                    }
                    r *= mul;
                    gg *= mul;
                    b *= mul;

                    if (glass > 0.f) {
                        // Reflection on the glass: a soft spot in the upper left (in screen coordinates, not image ones)
                        const float u = (float(x) - screen.halfW) / screen.halfW;
                        const float gx = u + 0.35f, gy = v + 0.55f;
                        const float spot = glass * 0.45f * std::exp(-(gx * gx + gy * gy) / (2.f * 0.32f * 0.32f));
                        r += (255.f - r) * spot;
                        gg += (255.f - gg) * spot;
                        b += (255.f - b) * spot;
                    }

                    d[x] = qRgba(clampByte(r * cov + kBezelR * (1.f - cov)), clampByte(gg * cov + kBezelG * (1.f - cov)),
                                 clampByte(b * cov + kBezelB * (1.f - cov)), 255);
                }
            });
        }
        return promise.isCanceled() ? QImage() : out;
    };
}

} // namespace kolorit
