#include "processing/Effects.h"

#include "core/ColorSpace.h"
#include "core/ParallelFor.h"
#include "core/PixelOps.h"
#include "processing/PaletteMapper.h"

#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

namespace kolorit::effects {

using colorspace::clampByte;

namespace {

/// Posterization table for a channel: value → nearest of levels steps
std::array<unsigned char, 256> posterizeLut(int levels)
{
    std::array<unsigned char, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const int q = int(std::lround(double(i) * (levels - 1) / 255.0));
        lut[size_t(i)] = static_cast<unsigned char>(q * 255 / (levels - 1));
    }
    return lut;
}

/// Median filter: every channel is replaced with the median of the (2r+1)² window. Removes speckle noise.
QImage medianPass(const QImage& in, int radius, QPromise<QImage>& promise, ProgressSpan span)
{
    const int width = in.width();
    const int height = in.height();
    QImage out(in.size(), QImage::Format_ARGB32);
    const RowWriter writer(out);
    const int size = 2 * radius + 1;
    const int count = size * size;

    parallelRows(height, promise, span, [&](int y) {
        const QRgb* c = constRow(in, y);
        QRgb* d = writer.row(y);
        std::vector<const QRgb*> rows(static_cast<size_t>(size));
        for (int k = 0; k < size; ++k)
            rows[size_t(k)] = constRow(in, std::clamp(y - radius + k, 0, height - 1));

        std::vector<unsigned char> r(static_cast<size_t>(count)), g(static_cast<size_t>(count)), b(static_cast<size_t>(count));
        for (int x = 0; x < width; ++x) {
            int n = 0;
            for (int ky = 0; ky < size; ++ky) {
                const QRgb* row = rows[size_t(ky)];
                for (int kx = 0; kx < size; ++kx) {
                    const QRgb p = row[std::clamp(x - radius + kx, 0, width - 1)];
                    r[size_t(n)] = static_cast<unsigned char>(qRed(p));
                    g[size_t(n)] = static_cast<unsigned char>(qGreen(p));
                    b[size_t(n)] = static_cast<unsigned char>(qBlue(p));
                    ++n;
                }
            }
            const auto mid = size_t(count / 2);
            std::nth_element(r.begin(), r.begin() + mid, r.end());
            std::nth_element(g.begin(), g.begin() + mid, g.end());
            std::nth_element(b.begin(), b.begin() + mid, b.end());
            d[x] = qRgba(r[mid], g[mid], b[mid], qAlpha(c[x]));
        }
    });
    return promise.isCanceled() ? QImage() : out;
}

/// Bilateral filter: averaging over a window weighted by distance and by color similarity,
/// so noise and blocking are smoothed while contrasty edges stay sharp.
QImage bilateralPass(const QImage& in, int radius, float sigmaRange, QPromise<QImage>& promise, ProgressSpan span)
{
    const int width = in.width();
    const int height = in.height();
    QImage out(in.size(), QImage::Format_ARGB32);
    const RowWriter writer(out);
    const int size = 2 * radius + 1;

    // Spatial weights of the window
    std::vector<float> spatial(size_t(size) * size_t(size));
    const float sigmaS = std::max(0.5f, radius * 0.5f);
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            spatial[size_t(dy + radius) * size_t(size) + size_t(dx + radius)] =
                std::exp(-float(dx * dx + dy * dy) / (2.f * sigmaS * sigmaS));

    // Color weights: a table over the squared RGB distance with a step of 8
    constexpr int kMaxDistSq = 3 * 255 * 255;
    std::vector<float> range(size_t(kMaxDistSq / 8) + 2);
    const float invRange = 1.f / (2.f * sigmaRange * sigmaRange);
    for (size_t i = 0; i < range.size(); ++i)
        range[i] = std::exp(-float(i * 8) * invRange);

    parallelRows(height, promise, span, [&](int y) {
        const QRgb* c = constRow(in, y);
        QRgb* d = writer.row(y);
        std::vector<const QRgb*> rows(static_cast<size_t>(size));
        for (int k = 0; k < size; ++k)
            rows[size_t(k)] = constRow(in, std::clamp(y - radius + k, 0, height - 1));

        for (int x = 0; x < width; ++x) {
            const int cr = qRed(c[x]), cg = qGreen(c[x]), cb = qBlue(c[x]);
            float sumW = 0.f, sumR = 0.f, sumG = 0.f, sumB = 0.f;
            for (int ky = 0; ky < size; ++ky) {
                const QRgb* row = rows[size_t(ky)];
                const float* sp = &spatial[size_t(ky) * size_t(size)];
                for (int kx = 0; kx < size; ++kx) {
                    const QRgb p = row[std::clamp(x - radius + kx, 0, width - 1)];
                    const int dr = qRed(p) - cr, dg = qGreen(p) - cg, db = qBlue(p) - cb;
                    const int distSq = dr * dr + dg * dg + db * db;
                    const float w = sp[kx] * range[size_t(distSq >> 3)];
                    sumW += w;
                    sumR += w * qRed(p);
                    sumG += w * qGreen(p);
                    sumB += w * qBlue(p);
                }
            }
            const float inv = 1.f / sumW;   // the center weight is always > 0
            d[x] = qRgba(clampByte(sumR * inv), clampByte(sumG * inv), clampByte(sumB * inv), qAlpha(c[x]));
        }
    });
    return promise.isCanceled() ? QImage() : out;
}

} // namespace

ImageOp invert()
{
    return [](const QImage& source, QPromise<QImage>& promise) {
        return mapPixels(source, promise, {0, 100}, [](QRgb c) {
            return qRgba(255 - qRed(c), 255 - qGreen(c), 255 - qBlue(c), qAlpha(c));
        });
    };
}

ImageOp grayscale()
{
    return [](const QImage& source, QPromise<QImage>& promise) {
        return mapPixels(source, promise, {0, 100}, [](QRgb c) {
            const int l = clampByte(colorspace::luma(c));
            return qRgba(l, l, l, qAlpha(c));
        });
    };
}

ImageOp sepia()
{
    return [](const QImage& source, QPromise<QImage>& promise) {
        return mapPixels(source, promise, {0, 100}, [](QRgb c) {
            const float r = qRed(c), g = qGreen(c), b = qBlue(c);
            return qRgba(clampByte(0.393f * r + 0.769f * g + 0.189f * b),
                         clampByte(0.349f * r + 0.686f * g + 0.168f * b),
                         clampByte(0.272f * r + 0.534f * g + 0.131f * b),
                         qAlpha(c));
        });
    };
}

ImageOp posterize(int levels)
{
    levels = std::clamp(levels, 2, 256);
    return [levels](const QImage& source, QPromise<QImage>& promise) {
        const auto lut = posterizeLut(levels);
        return mapPixels(source, promise, {0, 100}, [&](QRgb c) {
            return qRgba(lut[size_t(qRed(c))], lut[size_t(qGreen(c))], lut[size_t(qBlue(c))], qAlpha(c));
        });
    };
}

ImageOp pixelate(int blockSize)
{
    blockSize = std::max(1, blockSize);
    return [blockSize](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        QImage out(in.size(), QImage::Format_ARGB32);
        const RowWriter writer(out);
        const int width = in.width();
        const int height = in.height();
        const int blockRows = (height + blockSize - 1) / blockSize;

        // Parallel over rows of blocks: each row of blocks writes only its own lines
        parallelRows(blockRows, promise, {0, 100}, [&](int by) {
            const int y0 = by * blockSize;
            const int y1 = std::min(height, y0 + blockSize);
            for (int x0 = 0; x0 < width; x0 += blockSize) {
                const int x1 = std::min(width, x0 + blockSize);
                // Averaging in sRGB is deliberate; see the note in pixelArt()
                quint64 r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int y = y0; y < y1; ++y) {
                    const QRgb* row = constRow(in, y);
                    for (int x = x0; x < x1; ++x) {
                        r += qRed(row[x]);
                        g += qGreen(row[x]);
                        b += qBlue(row[x]);
                        a += qAlpha(row[x]);
                        ++n;
                    }
                }
                const QRgb avg = qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
                for (int y = y0; y < y1; ++y) {
                    QRgb* row = writer.row(y);
                    std::fill(row + x0, row + x1, avg);
                }
            }
        });
        return promise.isCanceled() ? QImage() : out;
    };
}

ImageOp adjust(double brightness, double contrast, double saturation)
{
    const float bright = float(std::clamp(brightness, -100.0, 100.0)) * 1.28f;
    // Contrast: -100 → 0 (flat gray), 0 → 1, +100 → 3
    const float contrastFactor = contrast >= 0 ? 1.f + float(contrast) / 50.f : 1.f + float(contrast) / 100.f;
    const float satFactor = saturation >= 0 ? 1.f + float(saturation) / 50.f : 1.f + float(saturation) / 100.f;

    return [=](const QImage& source, QPromise<QImage>& promise) {
        return mapPixels(source, promise, {0, 100}, [=](QRgb c) {
            float y, cb, cr;
            colorspace::rgbToYCbCr(float(qRed(c)), float(qGreen(c)), float(qBlue(c)), y, cb, cr);
            y = (y - 128.f) * contrastFactor + 128.f + bright;
            cb = (cb - 128.f) * satFactor + 128.f;
            cr = (cr - 128.f) * satFactor + 128.f;
            float r, g, b;
            colorspace::yCbCrToRgb(y, cb, cr, r, g, b);
            return qRgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(c));
        });
    };
}

ImageOp denoise(int medianRadius, int radius, double tolerance, int passes)
{
    medianRadius = std::clamp(medianRadius, 0, 3);
    radius = std::clamp(radius, 1, 8);
    passes = std::clamp(passes, 1, 4);
    const float sigmaRange = float(std::clamp(tolerance, 1.0, 100.0));

    return [=](const QImage& source, QPromise<QImage>& promise) -> QImage {
        QImage current = toArgb32(source);
        const int steps = (medianRadius > 0 ? 1 : 0) + passes;
        int step = 0;
        auto span = [&] {
            const ProgressSpan s{100 * step / steps, 100 * (step + 1) / steps};
            ++step;
            return s;
        };

        if (medianRadius > 0) {
            current = medianPass(current, medianRadius, promise, span());
            if (current.isNull())
                return {};
        }
        for (int i = 0; i < passes; ++i) {
            current = bilateralPass(current, radius, sigmaRange, promise, span());
            if (current.isNull())
                return {};
        }
        return current;
    };
}

ImageOp gradientMap(const GradientMapParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        // Gradient colors: the palette or black → white
        QVector<QRgb> stops = p.palette.colors();
        if (stops.isEmpty())
            stops = {qRgb(0, 0, 0), qRgb(255, 255, 255)};
        if (stops.size() == 1)
            stops.append(stops.first());
        if (p.reverse)
            std::reverse(stops.begin(), stops.end());

        // Table: luminance 0..255 → color. Balance shifts the midtones with a gamma.
        const double gamma = std::pow(2.0, -std::clamp(p.balance, -1.0, 1.0) * 1.5);
        const float opacity = float(std::clamp(p.opacity, 0.0, 1.0));
        std::array<QRgb, 256> lut{};
        const int last = int(stops.size()) - 1;
        for (int i = 0; i < 256; ++i) {
            const double t = std::pow(i / 255.0, gamma) * last;
            if (p.smooth) {
                const int a = std::clamp(int(std::floor(t)), 0, last);
                const int b = std::min(a + 1, last);
                const float f = float(t - a);
                lut[size_t(i)] = qRgb(clampByte(qRed(stops[a]) + (qRed(stops[b]) - qRed(stops[a])) * f),
                                      clampByte(qGreen(stops[a]) + (qGreen(stops[b]) - qGreen(stops[a])) * f),
                                      clampByte(qBlue(stops[a]) + (qBlue(stops[b]) - qBlue(stops[a])) * f));
            } else {
                lut[size_t(i)] = stops[std::clamp(int(std::lround(t)), 0, last)];
            }
        }

        return mapPixels(source, promise, {0, 100}, [&](QRgb c) {
            const QRgb mapped = lut[size_t(clampByte(colorspace::luma(c)))];
            if (opacity >= 1.f)
                return qRgba(qRed(mapped), qGreen(mapped), qBlue(mapped), qAlpha(c));
            return qRgba(clampByte(qRed(c) + (qRed(mapped) - qRed(c)) * opacity),
                         clampByte(qGreen(c) + (qGreen(mapped) - qGreen(c)) * opacity),
                         clampByte(qBlue(c) + (qBlue(mapped) - qBlue(c)) * opacity), qAlpha(c));
        });
    };
}

ImageOp duotone(const DuotoneParams& params)
{
    GradientMapParams gm;
    QVector<QRgb> stops{params.shadows.rgb()};
    if (params.useMidtones)
        stops.append(params.midtones.rgb());
    stops.append(params.highlights.rgb());
    gm.palette = Palette(stops);
    gm.smooth = true;
    gm.balance = params.balance;
    gm.opacity = params.opacity;
    return gradientMap(gm);
}

ImageOp pixelArt(const PixelArtParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        if (in.isNull())
            return {};
        const int width = in.width();
        const int height = in.height();
        const int block = std::max(1, p.blockSize);
        const int smallW = (width + block - 1) / block;
        const int smallH = (height + block - 1) / block;

        // ---------- 1. Downscaled image: average block color + quantization ----------
        //
        // The block is averaged in sRGB, and that is not an oversight. Adding sRGB bytes is wrong
        // where light physically adds up (downscaling for display, blur, glow): black and
        // white in equal parts give 128 instead of 188, which is 22.7 units of L*. The task
        // here is different: we need the color that replaces the block and is seen at full
        // size, the one closest to its pixels to the eye, not the sum of their light. L* is
        // concave in luminance, so the linear-light average is systematically lighter than
        // the perceptual one and loses in ΔE2000: 4.37 vs 3.94 for sRGB at block size 12,
        // 6.31 vs 5.53 at 32. A Lab average gives 3.92 and 5.49, beating sRGB by less than
        // a percent at the cost of three cube roots per pixel.
        QImage small(smallW, smallH, QImage::Format_ARGB32);
        {
            const RowWriter writer(small);
            std::optional<PaletteMapper> mapper;
            if (!p.palette.isEmpty())
                mapper.emplace(p.palette, p.model);
            const bool usePosterize = !mapper && p.levels >= 2;
            const auto lut = posterizeLut(usePosterize ? std::min(p.levels, 256) : 2);

            parallelRows(smallH, promise, {0, 45}, [&](int by) {
                QRgb* d = writer.row(by);
                const int y0 = by * block;
                const int y1 = std::min(height, y0 + block);
                for (int bx = 0; bx < smallW; ++bx) {
                    const int x0 = bx * block;
                    const int x1 = std::min(width, x0 + block);
                    quint64 r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int y = y0; y < y1; ++y) {
                        const QRgb* row = constRow(in, y);
                        for (int x = x0; x < x1; ++x) {
                            r += qRed(row[x]);
                            g += qGreen(row[x]);
                            b += qBlue(row[x]);
                            a += qAlpha(row[x]);
                            ++n;
                        }
                    }
                    QRgb avg = qRgb(int(r / n), int(g / n), int(b / n));
                    if (mapper)
                        avg = mapper->nearest(avg);
                    else if (usePosterize)
                        avg = qRgb(lut[size_t(qRed(avg))], lut[size_t(qGreen(avg))], lut[size_t(qBlue(avg))]);
                    d[bx] = (avg & 0x00FFFFFFu) | (QRgb(a / n) << 24);
                }
            });
        }
        if (promise.isCanceled())
            return {};

        // ---------- 2. Outlines: on which side of a block to draw the line ----------
        // Bit 0 is left, 1 top, 2 right, 3 bottom. The line goes on the darker of the two
        // neighbors, so an object gets an outline along its own edge rather than a double one.
        std::vector<uint8_t> edges(size_t(smallW) * size_t(smallH), 0);
        {
            const float maxDist = std::sqrt(3.f) * 255.f;
            const float thresholdSq = std::pow(float(std::clamp(p.outlineThreshold, 0.0, 1.0)) * maxDist, 2.f);
            auto differs = [&](QRgb a, QRgb b) {
                const int dr = qRed(a) - qRed(b), dg = qGreen(a) - qGreen(b), db = qBlue(a) - qBlue(b);
                return float(dr * dr + dg * dg + db * db) > thresholdSq;
            };
            const bool outlineEnabled = p.outlineThreshold < 1.0 && p.outlineDarkness > 0.0;

            parallelRows(smallH, promise, {45, 55}, [&](int by) {
                if (!outlineEnabled)
                    return;
                const QRgb* row = constRow(small, by);
                const QRgb* up = by > 0 ? constRow(small, by - 1) : nullptr;
                const QRgb* down = by + 1 < smallH ? constRow(small, by + 1) : nullptr;
                uint8_t* flags = &edges[size_t(by) * size_t(smallW)];
                for (int bx = 0; bx < smallW; ++bx) {
                    const QRgb c = row[bx];
                    const float l = colorspace::luma(c);
                    uint8_t f = 0;
                    if (bx > 0 && differs(c, row[bx - 1]) && l <= colorspace::luma(row[bx - 1]))
                        f |= 1u;
                    if (up && differs(c, up[bx]) && l <= colorspace::luma(up[bx]))
                        f |= 2u;
                    if (bx + 1 < smallW && differs(c, row[bx + 1]) && l <= colorspace::luma(row[bx + 1]))
                        f |= 4u;
                    if (down && differs(c, down[bx]) && l <= colorspace::luma(down[bx]))
                        f |= 8u;
                    flags[bx] = f;
                }
            });
        }
        if (promise.isCanceled())
            return {};

        // ---------- 3. Upscale back (crisp blocks) with outlines ----------
        QImage out(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(out);
            const int lineWidth = std::clamp(int(std::lround(block * std::clamp(p.outlineWidth, 0.0, 1.0))), 1, block);
            const float keep = 1.f - float(std::clamp(p.outlineDarkness, 0.0, 1.0));

            parallelRows(height, promise, {55, 100}, [&](int y) {
                QRgb* d = writer.row(y);
                const int by = y / block;
                const int iy = y - by * block;
                const QRgb* smallRow = constRow(small, by);
                const uint8_t* flags = &edges[size_t(by) * size_t(smallW)];
                for (int x = 0; x < width; ++x) {
                    const int bx = x / block;
                    const int ix = x - bx * block;
                    QRgb c = smallRow[bx];
                    const uint8_t f = flags[bx];
                    const bool outline = ((f & 1u) && ix < lineWidth) || ((f & 4u) && ix >= block - lineWidth) ||
                                         ((f & 2u) && iy < lineWidth) || ((f & 8u) && iy >= block - lineWidth);
                    if (outline)
                        c = qRgba(int(qRed(c) * keep), int(qGreen(c) * keep), int(qBlue(c) * keep), qAlpha(c));
                    d[x] = c;
                }
            });
        }
        return promise.isCanceled() ? QImage() : out;
    };
}

namespace {

inline float smoothstepf(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/// Coverage of a pixel by a circle of radius edge (fraction of R): 1 inside, 0 outside, a 1 px soft transition
inline float insideCoverage(float r, float edge, float radiusPx)
{
    return std::clamp((edge - r) * radiusPx + 0.5f, 0.f, 1.f);
}

/// HSV → RGB, h ∈ [0, 1), s and v ∈ [0, 1]; result in 0..255
inline void hsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    const float hh = (h - std::floor(h)) * 6.f;
    const int i = int(hh);
    const float f = hh - float(i);
    const float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    r *= 255.f;
    g *= 255.f;
    b *= 255.f;
}

/// Bilinear color sample from the image, clamped at the edges
inline void sampleBilinear(const QImage& image, float fx, float fy, float& r, float& g, float& b)
{
    const int w = image.width(), h = image.height();
    fx = std::clamp(fx, 0.f, float(w - 1));
    fy = std::clamp(fy, 0.f, float(h - 1));
    const int x0 = int(fx), y0 = int(fy);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float tx = fx - float(x0), ty = fy - float(y0);
    const QRgb* row0 = constRow(image, y0);
    const QRgb* row1 = constRow(image, y1);
    const QRgb c00 = row0[x0], c10 = row0[x1], c01 = row1[x0], c11 = row1[x1];
    auto lerp2 = [&](int a, int bb, int c, int d) {
        return (a + (bb - a) * tx) + ((c + (d - c) * tx) - (a + (bb - a) * tx)) * ty;
    };
    r = lerp2(qRed(c00), qRed(c10), qRed(c01), qRed(c11));
    g = lerp2(qGreen(c00), qGreen(c10), qGreen(c01), qGreen(c11));
    b = lerp2(qBlue(c00), qBlue(c10), qBlue(c01), qBlue(c11));
}

} // namespace

namespace {

/// Accent color "matching the image": the average saturated color, pushed to a pure hue.
/// For nearly gray pictures it is red, as on typical discs.
void accentColor(const QImage& image, float& r, float& g, float& b)
{
    double sr = 0, sg = 0, sb = 0, weight = 0;
    const int step = std::max(1, std::min(image.width(), image.height()) / 48);
    for (int y = 0; y < image.height(); y += step) {
        const QRgb* row = constRow(image, y);
        for (int x = 0; x < image.width(); x += step) {
            float h, s, v;
            colorspace::rgbToHsv(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), h, s, v);
            const double w = s * s * v;   // saturated and light colors weigh more
            sr += qRed(row[x]) * w;
            sg += qGreen(row[x]) * w;
            sb += qBlue(row[x]) * w;
            weight += w;
        }
    }
    if (weight < 1e-3) {
        r = 226.f;
        g = 58.f;
        b = 70.f;
        return;
    }
    float h, s, v;
    colorspace::rgbToHsv(int(sr / weight), int(sg / weight), int(sb / weight), h, s, v);
    hsvToRgb(h / (2.f * std::numbers::pi_v<float>), std::max(s, 0.8f), 0.92f, r, g, b);
}

} // namespace

ImageOp disc(const DiscParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        if (in.isNull())
            return {};
        const int width = in.width();
        const int height = in.height();
        const int side = std::min(width, height);
        if (side < 8)
            return {};

        QImage out(side, side, QImage::Format_ARGB32);
        const RowWriter writer(out);
        constexpr float pi = std::numbers::pi_v<float>;
        constexpr float twoPi = 2.f * pi;

        // Geometry: disc radius in pixels including the margin; relative radii are fractions of the radius
        const float margin = float(std::clamp(p.margin, 0.0, 0.25));
        const float R = float(side) * (0.5f - margin);
        const float cx = (side - 1) / 2.f;
        const float holeR = float(std::clamp(p.holeSize, 0.02, 0.6));
        float hubR = std::max(holeR + 0.01f, float(std::clamp(p.hubSize, 0.0, 0.8)));
        if (p.hub == DiscHub::PrintToCenter)
            hubR = holeR + 0.035f;
        const bool darkHub = p.hub == DiscHub::DarkMechanism;

        // Mapping disc pixels to the source
        const float k = float(std::min(width, height)) / (2.f * R * float(std::clamp(p.zoom, 0.1, 10.0)));
        const float srcCx = width / 2.f + float(p.offsetX) * width;
        const float srcCy = height / 2.f + float(p.offsetY) * height;

        // Lighting
        const float sheen = float(std::clamp(p.sheen, 0.0, 1.0));
        const float phi = float(p.sheenAngle) * pi / 180.f;
        const float gloss = float(std::clamp(p.gloss, 0.0, 1.0));
        const float tracks = float(std::clamp(p.tracks, 0.0, 1.0));
        const float glossX = 0.5f * std::cos(phi - 1.9f);   // glossy highlight point, fraction of the radius
        const float glossY = 0.5f * std::sin(phi - 1.9f);

        // Shadow: offset down and to the right, blurred over the margin width
        const float shadow = margin > 0.002f ? float(std::clamp(p.shadow, 0.0, 1.0)) : 0.f;
        const float shadowDx = margin * side * 0.45f;
        const float shadowDy = margin * side * 0.85f;
        const float shadowBlur = std::max(1.5f, margin * side * 0.9f);

        const bool transparent = p.transparentBackground;
        const float bgR = 20.f, bgG = 21.f, bgB = 26.f;

        float accR = 226.f, accG = 58.f, accB = 70.f;
        if (darkHub)
            accentColor(in, accR, accG, accB);

        parallelRows(side, promise, {0, 100}, [&](int y) {
            QRgb* d = writer.row(y);
            const float py = float(y) - cx;
            for (int x = 0; x < side; ++x) {
                const float px = float(x) - cx;
                const float dx = px / R;
                const float dy = py / R;
                const float r = std::sqrt(dx * dx + dy * dy);

                // ---------- Shadow under the disc ----------
                float shadowA = 0.f;
                if (shadow > 0.f) {
                    const float sd = std::sqrt((px - shadowDx) * (px - shadowDx) + (py - shadowDy) * (py - shadowDy));
                    shadowA = shadow * 0.55f * (1.f - smoothstepf(R - shadowBlur, R + shadowBlur, sd));
                }

                // ---------- Disc ----------
                const float discCov = insideCoverage(r, 1.f, R);
                const float holeCov = 1.f - insideCoverage(r, holeR, R);
                float discA = discCov * holeCov;
                float cr = 0.f, cg = 0.f, cb = 0.f;

                if (discA > 0.f) {
                    const float theta = std::atan2(dy, dx);

                    // --- Print: the image with slight volumetric shading towards the edge ---
                    sampleBilinear(in, srcCx + dx * R * k, srcCy + dy * R * k, cr, cg, cb);
                    const float depth = 0.92f + 0.08f * (1.f - r);
                    cr *= depth;
                    cg *= depth;
                    cb *= depth;

                    // --- Diffraction rainbow: two lobes, concentric color arcs and thin rays ---
                    if (sheen > 0.f) {
                        float lobe = std::max(0.f, std::cos(2.f * (theta - phi)));
                        lobe = lobe * lobe * lobe * lobe;
                        const float radial = 0.3f + 0.7f * smoothstepf(hubR, 1.f, r);
                        const float s = sheen * lobe * radial;
                        if (s > 0.002f) {
                            float hr, hg, hb;
                            hsvToRgb(0.62f + 2.4f * r + 0.05f * std::sin(7.f * theta), 0.7f, 1.f, hr, hg, hb);
                            const float rays = 0.86f + 0.14f * std::sin(theta * 220.f) * std::sin(theta * 37.f + r * 9.f);
                            const float mix = s * 0.6f;
                            cr += (hr * rays - cr) * mix + s * s * 60.f;
                            cg += (hg * rays - cg) * mix + s * s * 60.f;
                            cb += (hb * rays - cb) * mix + s * s * 60.f;
                        }
                    }

                    // --- Tracks: fine modulation with a slow envelope so it does not look like a grid ---
                    if (tracks > 0.f) {
                        const float fine = std::sin(r * R * twoPi / 3.f);
                        const float envelope = 0.6f + 0.4f * std::sin(r * R * twoPi / 17.f);
                        const float mod = 1.f + tracks * 0.09f * fine * envelope;
                        cr *= mod;
                        cg *= mod;
                        cb *= mod;
                    }

                    // --- Glossy highlight from the light source ---
                    if (gloss > 0.f) {
                        const float gx = dx - glossX, gy = dy - glossY;
                        const float g = gloss * 0.4f * std::exp(-(gx * gx + gy * gy) / (2.f * 0.3f * 0.3f));
                        cr += (255.f - cr) * g;
                        cg += (255.f - cg) * g;
                        cb += (255.f - cb) * g;
                    }

                    // --- Lead-out ring: barely lighter ---
                    const float leadOut = std::exp(-std::pow((r - 0.95f) * R / 1.5f, 2.f)) * 0.06f;
                    cr += (255.f - cr) * leadOut;
                    cg += (255.f - cg) * leadOut;
                    cb += (255.f - cb) * leadOut;

                    // --- Edge: a clear rim without print, a dark chamfer, a light lip ---
                    const float rimMix = smoothstepf(0.962f, 0.978f, r);
                    cr += (206.f - cr) * rimMix * 0.85f;
                    cg += (208.f - cg) * rimMix * 0.85f;
                    cb += (214.f - cb) * rimMix * 0.85f;
                    discA *= 1.f - rimMix * 0.45f;
                    const float bevel = std::exp(-std::pow((r - 0.978f) * R / 0.9f, 2.f)) * 0.55f;
                    cr *= 1.f - bevel;
                    cg *= 1.f - bevel;
                    cb *= 1.f - bevel;
                    const float edgeLight = smoothstepf(0.992f, 1.f, r) * 0.6f;
                    cr += (250.f - cr) * edgeLight;
                    cg += (250.f - cg) * edgeLight;
                    cb += (252.f - cb) * edgeLight;

                    // --- Print boundary at the hub: a thin dark line ---
                    const float printEdge = std::exp(-std::pow((r - hubR) * R / 1.2f, 2.f)) * 0.35f;
                    cr *= 1.f - printEdge;
                    cg *= 1.f - printEdge;
                    cb *= 1.f - printEdge;

                    // --- Hub ---
                    const float hubCov = 1.f - insideCoverage(r, hubR, R);   // 1 in the print area
                    if (hubCov < 1.f) {
                        float hr, hg, hb, ha;
                        if (darkHub) {
                            // Dark mechanism: concentric grooves, spokes, accent ring, metallic edges
                            const float grooves = 1.f + 0.12f * std::sin(r * R * twoPi / 4.f);
                            const float spokes = std::pow(std::max(0.f, std::cos(12.f * theta)), 6.f) * 0.10f;
                            const float base = grooves + spokes;
                            hr = 40.f * base;
                            hg = 42.f * base;
                            hb = 48.f * base;
                            const float ringR = holeR + (hubR - holeR) * 0.45f;
                            const float ringW = std::max(1.5f, 0.012f * R);
                            const float accent = 1.f - smoothstepf(ringW, ringW * 1.8f, std::fabs(r - ringR) * R);
                            hr += (accR - hr) * accent;
                            hg += (accG - hg) * accent;
                            hb += (accB - hb) * accent;
                            const float metal = std::max(std::exp(-std::pow((r - hubR) * R / 1.2f, 2.f)),
                                                         std::exp(-std::pow((r - holeR) * R / 1.2f, 2.f))) * 0.7f;
                            hr += (150.f - hr) * metal;
                            hg += (152.f - hg) * metal;
                            hb += (158.f - hb) * metal;
                            ha = 1.f;
                        } else {
                            // Clear plastic: light rings at the boundaries, stacking ring, chamfered hole
                            const float stackR = holeR + (hubR - holeR) * 0.55f;
                            const float ringA = std::exp(-std::pow((r - holeR) * R / 1.1f, 2.f));
                            const float ringB = std::exp(-std::pow((r - hubR) * R / 1.1f, 2.f));
                            const float stack = std::exp(-std::pow((r - stackR) * R / 1.3f, 2.f)) * 0.8f;
                            const float ring = std::max({ringA, ringB, stack});
                            hr = 222.f + (246.f - 222.f) * ring;
                            hg = 226.f + (246.f - 226.f) * ring;
                            hb = 232.f + (249.f - 232.f) * ring;
                            ha = 0.42f + 0.4f * ring;
                            const float holeBevel = std::exp(-std::pow((r - holeR - 0.7f / R) * R / 0.8f, 2.f)) * 0.5f;
                            hr *= 1.f - holeBevel;
                            hg *= 1.f - holeBevel;
                            hb *= 1.f - holeBevel;
                            ha += holeBevel * 0.4f;
                        }
                        cr = hr + (cr - hr) * hubCov;
                        cg = hg + (cg - hg) * hubCov;
                        cb = hb + (cb - hb) * hubCov;
                        discA *= ha + (1.f - ha) * hubCov;
                    }
                }

                // ---------- Composition: disc over shadow over background ----------
                float outR, outG, outB, outA;
                if (transparent) {
                    outA = discA + shadowA * (1.f - discA);
                    if (outA > 0.f) {
                        outR = (cr * discA) / outA;
                        outG = (cg * discA) / outA;
                        outB = (cb * discA) / outA;
                    } else {
                        outR = outG = outB = 0.f;
                    }
                } else {
                    const float underR = bgR * (1.f - shadowA);
                    const float underG = bgG * (1.f - shadowA);
                    const float underB = bgB * (1.f - shadowA);
                    outR = cr * discA + underR * (1.f - discA);
                    outG = cg * discA + underG * (1.f - discA);
                    outB = cb * discA + underB * (1.f - discA);
                    outA = 1.f;
                }
                d[x] = qRgba(clampByte(outR), clampByte(outG), clampByte(outB), clampByte(outA * 255.f));
            }
        });
        return promise.isCanceled() ? QImage() : out;
    };
}

namespace {

/// Coverage of a halftone dot within a pixel: 1 is ink, 0 is paper.
///
/// The grid is rotated by the given angle, the distance to the cell center is measured,
/// and the dot grows from it as the source gets darker. The dot edge is blurred over a
/// width inverse to the sharpness; otherwise a coarse screen shows jagged steps.
float halftoneCoverage(double x, double y, double density, double cell, double angleDeg, double sharpness)
{
    const double angle = angleDeg * std::numbers::pi / 180.0;
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    const double rx = (x * cosA - y * sinA) / cell;
    const double ry = (x * sinA + y * cosA) / cell;

    // Distance to the center of its cell, normalized to half the diagonal
    const double dx = rx - std::floor(rx) - 0.5;
    const double dy = ry - std::floor(ry) - 0.5;
    const double distance = std::sqrt(dx * dx + dy * dy) / 0.7071;

    // Dot radius chosen so its area equals the required ink density
    const double radius = std::sqrt(std::clamp(density, 0.0, 1.0));
    const double edge = std::max(0.02, (1.0 - std::clamp(sharpness, 0.0, 1.0)) * 0.6 + 0.02);
    return float(std::clamp((radius - distance) / edge + 0.5, 0.0, 1.0));
}

} // namespace

ImageOp halftone(const HalftoneParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        QImage out(in.size(), QImage::Format_ARGB32);
        const RowWriter writer(out);
        const int width = in.width();
        const double cell = std::max(2.0, p.cellSize);

        // Ink angles as in offset printing: with this rotation the dots form rosettes
        // and produce no moiré. Black goes at the given angle, the others are offset from it.
        const double angleK = p.angle;
        const double angleC = p.angle + 60.0;
        const double angleM = p.angle - 60.0;
        const double angleY = p.angle - 15.0;

        parallelRows(in.height(), promise, {0, 100}, [&](int y) {
            const QRgb* s = constRow(in, y);
            QRgb* d = writer.row(y);
            for (int x = 0; x < width; ++x) {
                const QRgb c = s[x];
                float r = 0.f, g = 0.f, b = 0.f;
                if (p.colorSeparation) {
                    // CMYK separation: three inks give the color, black gives depth
                    const double rf = qRed(c) / 255.0, gf = qGreen(c) / 255.0, bf = qBlue(c) / 255.0;
                    const double k = 1.0 - std::max({rf, gf, bf});
                    const double denom = std::max(1e-6, 1.0 - k);
                    const double cyan = (1.0 - rf - k) / denom;
                    const double magenta = (1.0 - gf - k) / denom;
                    const double yellow = (1.0 - bf - k) / denom;

                    const float dotC = halftoneCoverage(x, y, cyan, cell, angleC, p.sharpness);
                    const float dotM = halftoneCoverage(x, y, magenta, cell, angleM, p.sharpness);
                    const float dotY = halftoneCoverage(x, y, yellow, cell, angleY, p.sharpness);
                    const float dotK = halftoneCoverage(x, y, k, cell, angleK, p.sharpness);

                    // Inks cover the paper one after another, so their fractions multiply
                    r = (1.f - dotC) * (1.f - dotK);
                    g = (1.f - dotM) * (1.f - dotK);
                    b = (1.f - dotY) * (1.f - dotK);
                } else {
                    const double density = 1.0 - colorspace::luma(c) / 255.0;
                    const float dot = halftoneCoverage(x, y, density, cell, angleK, p.sharpness);
                    r = g = b = 1.f - dot;
                }
                if (!p.onWhite) {
                    r = 1.f - r;
                    g = 1.f - g;
                    b = 1.f - b;
                }
                d[x] = qRgba(clampByte(r * 255.f), clampByte(g * 255.f), clampByte(b * 255.f), qAlpha(c));
            }
        });
        return promise.isCanceled() ? QImage() : out;
    };
}

ImageOp colorSplash(const ColorSplashParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const double center = p.hue;
        const double tolerance = std::max(0.0, p.tolerance);
        const double softness = std::max(0.0, p.softness);
        const double minSaturation = std::clamp(p.minSaturation, 0.0, 1.0);

        return mapPixels(source, promise, {0, 100}, [&](QRgb c) {
            float h = 0.f, sat = 0.f, l = 0.f;
            colorspace::rgbToHsl(qRed(c), qGreen(c), qBlue(c), h, sat, l);
            const double hue = double(h) * 180.0 / std::numbers::pi;

            // Distance around the hue circle; otherwise red would split at the 0°/360° seam
            double distance = std::fabs(hue - center);
            if (distance > 180.0)
                distance = 360.0 - distance;

            // Share of "colorfulness": 1 keeps the color, 0 desaturates
            double keep = distance <= tolerance
                              ? 1.0
                              : (softness <= 0.0 ? 0.0 : std::clamp(1.0 - (distance - tolerance) / softness, 0.0, 1.0));
            if (sat < minSaturation)
                keep = 0.0;   // nearly gray pixels do not count as falling into the range
            if (p.invert)
                keep = 1.0 - keep;

            const float gray = colorspace::luma(c);
            return qRgba(clampByte(gray + (qRed(c) - gray) * float(keep)),
                         clampByte(gray + (qGreen(c) - gray) * float(keep)),
                         clampByte(gray + (qBlue(c) - gray) * float(keep)), qAlpha(c));
        });
    };
}

ImageOp levels(const LevelsParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const double gamma = std::clamp(p.gamma, 0.1, 10.0);
        const double outBlack = std::clamp(p.outputBlack, 0.0, 255.0);
        const double outWhite = std::clamp(p.outputWhite, 0.0, 255.0);

        // A table per channel: the common black point plus its offset for this channel
        const double blackShift[3] = {p.redBlack, p.greenBlack, p.blueBlack};
        std::array<std::array<uint8_t, 256>, 3> lut{};
        for (int channel = 0; channel < 3; ++channel) {
            const double black = std::clamp(p.blackPoint + blackShift[size_t(channel)], 0.0, 254.0);
            const double white = std::max(black + 1.0, std::clamp(p.whitePoint, 1.0, 255.0));
            for (int i = 0; i < 256; ++i) {
                const double normalized = std::clamp((i - black) / (white - black), 0.0, 1.0);
                const double corrected = std::pow(normalized, 1.0 / gamma);
                lut[size_t(channel)][size_t(i)] = uint8_t(clampByte(float(outBlack + corrected * (outWhite - outBlack))));
            }
        }

        return mapPixels(source, promise, {0, 100}, [&](QRgb c) {
            return qRgba(lut[0][size_t(qRed(c))], lut[1][size_t(qGreen(c))], lut[2][size_t(qBlue(c))], qAlpha(c));
        });
    };
}

// ---------------------------------------------------------------------------
// 3D LUT in the .cube format
// ---------------------------------------------------------------------------

CubeLut CubeLut::load(const QString& path, QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
        return CubeLut{};
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QObject::tr("could not open the file: %1").arg(file.errorString()));

    CubeLut lut;
    int declaredSize = 0;
    bool oneDimensional = false;
    double domainMin = 0.0;
    double domainMax = 1.0;
    QVector<QRgb> nodes;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        const int comment = line.indexOf(QLatin1Char('#'));
        if (comment >= 0)
            line = line.left(comment).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String("TITLE"), Qt::CaseInsensitive)) {
            lut.m_title = line.mid(5).trimmed().remove(QLatin1Char('"'));
            continue;
        }
        if (line.startsWith(QLatin1String("LUT_3D_SIZE"), Qt::CaseInsensitive)) {
            declaredSize = line.mid(11).trimmed().toInt();
            continue;
        }
        if (line.startsWith(QLatin1String("LUT_1D_SIZE"), Qt::CaseInsensitive)) {
            declaredSize = line.mid(11).trimmed().toInt();
            oneDimensional = true;
            continue;
        }
        if (line.startsWith(QLatin1String("DOMAIN_MIN"), Qt::CaseInsensitive)) {
            domainMin = line.mid(10).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0).toDouble();
            continue;
        }
        if (line.startsWith(QLatin1String("DOMAIN_MAX"), Qt::CaseInsensitive)) {
            domainMax = line.mid(10).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0).toDouble();
            continue;
        }
        if (line.at(0).isLetter())
            continue;   // other keyword lines of the format are skipped

        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            continue;
        const double scale = domainMax > domainMin ? 1.0 / (domainMax - domainMin) : 1.0;
        const double r = (parts[0].toDouble() - domainMin) * scale;
        const double g = (parts[1].toDouble() - domainMin) * scale;
        const double b = (parts[2].toDouble() - domainMin) * scale;
        nodes.append(qRgb(clampByte(float(r * 255.0)), clampByte(float(g * 255.0)), clampByte(float(b * 255.0))));
    }

    if (declaredSize < 2 || declaredSize > 128)
        return fail(QObject::tr("the table size (LUT_3D_SIZE) is missing or implausible"));

    if (oneDimensional) {
        // A one-dimensional table is expanded into a 3D one: the channels are independent
        if (nodes.size() < declaredSize)
            return fail(QObject::tr("the file has %1 rows instead of %2").arg(nodes.size()).arg(declaredSize));
        const QVector<QRgb> curve = nodes;
        nodes.clear();
        nodes.reserve(declaredSize * declaredSize * declaredSize);
        for (int bi = 0; bi < declaredSize; ++bi)
            for (int gi = 0; gi < declaredSize; ++gi)
                for (int ri = 0; ri < declaredSize; ++ri)
                    nodes.append(qRgb(qRed(curve[ri]), qGreen(curve[gi]), qBlue(curve[bi])));
    }

    const qsizetype expected = qsizetype(declaredSize) * declaredSize * declaredSize;
    if (nodes.size() != expected)
        return fail(QObject::tr("the file has %1 nodes instead of %2").arg(nodes.size()).arg(expected));

    lut.m_size = declaredSize;
    lut.m_nodes = std::move(nodes);
    if (errorMessage)
        errorMessage->clear();
    return lut;
}

CubeLut CubeLut::fromOp(const ImageOp& op, int size)
{
    CubeLut lut;
    lut.m_size = std::clamp(size, 2, 64);

    // The color cube is laid out as a strip image and run through the operation as a
    // whole, so the table comes from the same code that processes pictures
    const int n = lut.m_size;
    const int total = n * n * n;
    QImage cube(total, 1, QImage::Format_ARGB32);
    auto* row = reinterpret_cast<QRgb*>(cube.bits());
    int index = 0;
    for (int bi = 0; bi < n; ++bi)
        for (int gi = 0; gi < n; ++gi)
            for (int ri = 0; ri < n; ++ri)
                row[index++] = qRgb(int(std::lround(255.0 * ri / (n - 1))), int(std::lround(255.0 * gi / (n - 1))),
                                    int(std::lround(255.0 * bi / (n - 1))));

    QPromise<QImage> promise;
    promise.start();
    promise.setProgressRange(0, 100);
    const QImage mapped = op(cube, promise);
    promise.finish();
    if (mapped.isNull() || mapped.width() != total)
        return {};

    const auto* mappedRow = reinterpret_cast<const QRgb*>(mapped.constBits());
    lut.m_nodes.resize(total);
    for (int i = 0; i < total; ++i)
        lut.m_nodes[i] = mappedRow[i];
    return lut;
}

bool CubeLut::save(const QString& path, QString* errorMessage) const
{
    if (!isValid()) {
        if (errorMessage)
            *errorMessage = QObject::tr("the table is empty");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream << "# Kolorit\n";
    stream << "TITLE \"" << (m_title.isEmpty() ? QStringLiteral("Kolorit") : m_title) << "\"\n";
    stream << "LUT_3D_SIZE " << m_size << "\n";
    stream << "DOMAIN_MIN 0.0 0.0 0.0\n";
    stream << "DOMAIN_MAX 1.0 1.0 1.0\n";
    for (QRgb c : m_nodes) {
        stream << Qt::fixed << qSetRealNumberPrecision(6) << qRed(c) / 255.0 << ' ' << qGreen(c) / 255.0 << ' '
               << qBlue(c) / 255.0 << '\n';
    }
    stream.flush();
    if (errorMessage)
        errorMessage->clear();
    return file.error() == QFileDevice::NoError;
}

ImageOp CubeLut::makeOp(double strength) const
{
    return [lut = *this, strength](const QImage& source, QPromise<QImage>& promise) -> QImage {
        if (!lut.isValid())
            return toArgb32(source);

        const int n = lut.size();
        const float mix = float(std::clamp(strength, 0.0, 1.0));
        const float scale = float(n - 1) / 255.f;

        return mapPixels(source, promise, {0, 100}, [&](QRgb c) {
            // Trilinear interpolation over the eight nearest nodes of the cube
            const float fr = qRed(c) * scale;
            const float fg = qGreen(c) * scale;
            const float fb = qBlue(c) * scale;
            const int r0 = std::min(n - 1, int(fr)), r1 = std::min(n - 1, r0 + 1);
            const int g0 = std::min(n - 1, int(fg)), g1 = std::min(n - 1, g0 + 1);
            const int b0 = std::min(n - 1, int(fb)), b1 = std::min(n - 1, b0 + 1);
            const float tr = fr - r0, tg = fg - g0, tb = fb - b0;

            const auto node = [&](int ri, int gi, int bi) { return lut.m_nodes[size_t((bi * n + gi) * n + ri)]; };
            const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

            float channels[3] = {0.f, 0.f, 0.f};
            for (int k = 0; k < 3; ++k) {
                const auto at = [&](int ri, int gi, int bi) {
                    const QRgb v = node(ri, gi, bi);
                    return float(k == 0 ? qRed(v) : (k == 1 ? qGreen(v) : qBlue(v)));
                };
                const float c00 = lerp(at(r0, g0, b0), at(r1, g0, b0), tr);
                const float c10 = lerp(at(r0, g1, b0), at(r1, g1, b0), tr);
                const float c01 = lerp(at(r0, g0, b1), at(r1, g0, b1), tr);
                const float c11 = lerp(at(r0, g1, b1), at(r1, g1, b1), tr);
                channels[k] = lerp(lerp(c00, c10, tg), lerp(c01, c11, tg), tb);
            }

            if (mix < 1.f) {
                channels[0] = qRed(c) + (channels[0] - qRed(c)) * mix;
                channels[1] = qGreen(c) + (channels[1] - qGreen(c)) * mix;
                channels[2] = qBlue(c) + (channels[2] - qBlue(c)) * mix;
            }
            return qRgba(clampByte(channels[0]), clampByte(channels[1]), clampByte(channels[2]), qAlpha(c));
        });
    };
}

} // namespace kolorit::effects
