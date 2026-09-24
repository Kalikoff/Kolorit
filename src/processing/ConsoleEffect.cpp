#include "processing/ConsoleEffect.h"

#include "core/BuiltinPalettes.h"
#include "core/ColorSpace.h"
#include "core/ParallelFor.h"
#include "core/PixelOps.h"

#include <QObject>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace kolorit {

namespace {

bool isGameBoy(Console c)
{
    return c == Console::GameBoy || c == Console::GameBoyPocket;
}

/// ZX Spectrum brightness by color index: 0 is normal, 1 is BRIGHT; black fits both
int spectrumBright(int index)
{
    return index >= 8 ? 1 : 0;
}

} // namespace

QString consoleName(Console console)
{
    switch (console) {
    case Console::GameBoy:       return QObject::tr("Game Boy (green)");
    case Console::GameBoyPocket: return QObject::tr("Game Boy Pocket (gray)");
    case Console::Nes:           return QObject::tr("NES / Famicom");
    case Console::ZxSpectrum:    return QObject::tr("ZX Spectrum");
    }
    return {};
}

QSize consoleResolution(Console console)
{
    switch (console) {
    case Console::GameBoy:
    case Console::GameBoyPocket: return {160, 144};
    case Console::Nes:           return {256, 240};
    case Console::ZxSpectrum:    return {256, 192};
    }
    return {160, 144};
}

Palette consolePalette(Console console)
{
    switch (console) {
    case Console::GameBoy:       return builtin::gameBoy();
    case Console::GameBoyPocket: return builtin::gameBoyPocket();
    case Console::Nes:           return builtin::nes();
    case Console::ZxSpectrum:    return builtin::zxSpectrum();
    }
    return {};
}

QList<Console> allConsoles()
{
    return {Console::GameBoy, Console::GameBoyPocket, Console::Nes, Console::ZxSpectrum};
}

ImageOp consoleOp(const ConsoleParams& params)
{
    return [p = params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        if (in.isNull())
            return {};
        const int width = in.width();
        const int height = in.height();

        // ---------- 1. Downscale to the console resolution (keeping the aspect ratio) ----------
        const QSize target = consoleResolution(p.console);
        QImage low = in.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_ARGB32);
        if (low.isNull() || low.width() < 1 || low.height() < 1)
            return {};
        const int lw = low.width();
        const int lh = low.height();

        const Palette palette = consolePalette(p.console);
        const int colorCount = palette.size();
        std::vector<QRgb> colors(palette.colors().begin(), palette.colors().end());
        std::vector<Vec3> lab;
        lab.reserve(colors.size());
        for (QRgb c : colors)
            lab.push_back(colorspace::toModel(c, ColorModel::Lab));

        // Error diffusion is not needed here: the per-block color selection that follows
        // reassigns the pixels anyway and destroys the accumulated residual
        const Dithering dithering = isErrorDiffusion(p.dithering) ? Dithering::Bayer4 : p.dithering;
        const OrderedDither dither(dithering, float(std::clamp(p.ditherStrength, 0, 128)));
        const float contrastFactor = 1.f + float(std::clamp(p.contrast, -1.0, 1.0));

        // ---------- 2. Quantization with ordered dithering ----------
        std::vector<uint8_t> index(size_t(lw) * size_t(lh));
        const PaletteMapper mapper(palette, ColorModel::Lab);
        const bool gameBoy = isGameBoy(p.console);

        parallelRows(lh, promise, {0, 50}, [&](int y) {
            const QRgb* s = constRow(low, y);
            uint8_t* d = &index[size_t(y) * size_t(lw)];
            for (int x = 0; x < lw; ++x) {
                // The threshold is the same for all three channels: a console picture is recognizable
                // by exactly this luminance grid, and the per-block color selection comes next anyway
                const float t = dither.at(x, y, 0);
                float r = (qRed(s[x]) - 128.f) * contrastFactor + 128.f + t;
                float g = (qGreen(s[x]) - 128.f) * contrastFactor + 128.f + t;
                float b = (qBlue(s[x]) - 128.f) * contrastFactor + 128.f + t;
                if (gameBoy) {
                    // The Game Boy sees only luminance: 4 levels from dark to light
                    const float l = 0.299f * r + 0.587f * g + 0.114f * b;
                    d[x] = uint8_t(std::clamp(int(std::lround(l / 255.f * (colorCount - 1))), 0, colorCount - 1));
                } else {
                    const QRgb wanted = qRgb(colorspace::clampByte(r), colorspace::clampByte(g), colorspace::clampByte(b));
                    d[x] = uint8_t(mapper.nearestIndex(wanted));
                }
            }
        });
        if (promise.isCanceled())
            return {};

        // ---------- 3. Limits on colors per block ----------
        if (p.attributeLimits && !gameBoy) {
            const int block = p.console == Console::ZxSpectrum ? 8 : 16;
            const int maxColors = p.console == Console::ZxSpectrum ? 2 : 4;
            const int blockRows = (lh + block - 1) / block;
            const int blockCols = (lw + block - 1) / block;

            parallelRows(blockRows, promise, {50, 70}, [&](int by) {
                std::vector<int> histogram(static_cast<size_t>(colorCount));
                std::vector<int> allowed;
                const int y0 = by * block;
                const int y1 = std::min(lh, y0 + block);
                for (int bx = 0; bx < blockCols; ++bx) {
                    const int x0 = bx * block;
                    const int x1 = std::min(lw, x0 + block);

                    std::fill(histogram.begin(), histogram.end(), 0);
                    for (int y = y0; y < y1; ++y)
                        for (int x = x0; x < x1; ++x)
                            ++histogram[index[size_t(y) * size_t(lw) + size_t(x)]];

                    // Indices by decreasing frequency
                    std::vector<int> order;
                    for (int c = 0; c < colorCount; ++c)
                        if (histogram[size_t(c)] > 0)
                            order.push_back(c);
                    std::sort(order.begin(), order.end(),
                              [&histogram](int a, int b) { return histogram[size_t(a)] > histogram[size_t(b)]; });

                    allowed.clear();
                    if (p.console == Console::ZxSpectrum) {
                        // INK and PAPER of the same brightness; black is compatible with either
                        const int first = order.empty() ? 0 : order.front();
                        int bright = spectrumBright(first);
                        if (first == 0) {
                            for (int c : order)
                                if (c != 0) {
                                    bright = spectrumBright(c);
                                    break;
                                }
                        }
                        allowed.push_back(first);
                        for (int c : order) {
                            if (c == first)
                                continue;
                            if (c == 0 || spectrumBright(c) == bright) {
                                allowed.push_back(c);
                                break;
                            }
                        }
                    } else {
                        for (int c : order) {
                            allowed.push_back(c);
                            if (int(allowed.size()) >= maxColors)
                                break;
                        }
                    }
                    if (allowed.empty())
                        allowed.push_back(0);

                    // Reassign the pixels of the block to the nearest allowed color
                    for (int y = y0; y < y1; ++y) {
                        for (int x = x0; x < x1; ++x) {
                            uint8_t& idx = index[size_t(y) * size_t(lw) + size_t(x)];
                            if (std::find(allowed.begin(), allowed.end(), int(idx)) != allowed.end())
                                continue;
                            int best = allowed.front();
                            float bestD = std::numeric_limits<float>::max();
                            for (int c : allowed) {
                                const float d = colorspace::distanceSq(lab[size_t(idx)], lab[size_t(c)]);
                                if (d < bestD) {
                                    bestD = d;
                                    best = c;
                                }
                            }
                            idx = uint8_t(best);
                        }
                    }
                }
            });
            if (promise.isCanceled())
                return {};
        }

        // ---------- 4. Scale back up with crisp pixels ----------
        QImage out(width, height, QImage::Format_ARGB32);
        const RowWriter writer(out);
        // Integer upscaling: the frame is centerd with black borders around it
        int scale = std::max(1, std::min(width / lw, height / lh));
        const int outW = p.integerScale ? lw * scale : width;
        const int outH = p.integerScale ? lh * scale : height;
        const int offX = (width - outW) / 2;
        const int offY = (height - outH) / 2;
        const QRgb border = qRgb(8, 8, 10);

        parallelRows(height, promise, {70, 100}, [&](int y) {
            QRgb* d = writer.row(y);
            const int ly = y - offY;
            if (ly < 0 || ly >= outH) {
                std::fill(d, d + width, border);
                return;
            }
            const int sy = std::min(lh - 1, int(qint64(ly) * lh / outH));
            const uint8_t* row = &index[size_t(sy) * size_t(lw)];
            for (int x = 0; x < width; ++x) {
                const int lx = x - offX;
                if (lx < 0 || lx >= outW) {
                    d[x] = border;
                    continue;
                }
                const int sx = std::min(lw - 1, int(qint64(lx) * lw / outW));
                d[x] = colors[row[sx]];
            }
        });
        return promise.isCanceled() ? QImage() : out;
    };
}

} // namespace kolorit
