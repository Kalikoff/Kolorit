#include "processing/PaletteMapper.h"

#include "core/ParallelFor.h"
#include "core/PixelOps.h"
#include "core/Random.h"

#include <QObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

namespace kolorit {

namespace {

// Cache key: 6 bits per channel (see the explanation in PaletteMapper.h)
constexpr int kCacheBits = 6;
constexpr int kCacheShift = 8 - kCacheBits;
constexpr size_t kCacheSize = size_t(1) << (3 * kCacheBits);

/// Color → cache bucket number
inline size_t cacheKey(QRgb rgb)
{
    return (size_t(qRed(rgb) >> kCacheShift) << (2 * kCacheBits)) |
           (size_t(qGreen(rgb) >> kCacheShift) << kCacheBits) | size_t(qBlue(rgb) >> kCacheShift);
}

/// Bucket number → its center color. The nearest palette color is searched for this
/// color, so the result is the same whatever order the pixels are visited in.
inline QRgb cacheCenter(size_t key)
{
    constexpr int half = 1 << (kCacheShift - 1);
    constexpr size_t mask = (size_t(1) << kCacheBits) - 1;
    return qRgb(int(((key >> (2 * kCacheBits)) & mask) << kCacheShift) | half,
                int(((key >> kCacheBits) & mask) << kCacheShift) | half,
                int((key & mask) << kCacheShift) | half);
}

/// Bayer matrices (normalized thresholds are built at run time)
constexpr int kBayer2[2][2] = {{0, 2}, {3, 1}};
constexpr int kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
constexpr int kBayer8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},   {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},  {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},   {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21},
};

constexpr int kBlueNoiseSize = 64;

/// 64×64 blue-noise threshold matrix: the rank of every cell, 0 to 4095.
///
/// A Bayer matrix produces a regular grid that the eye picks up easily; error diffusion
/// has no grid but drags "worms" along and needs a sequential pass. Blue noise is the
/// compromise: the thresholds are placed so the spectrum has no low frequencies, so the
/// pattern dissolves into an even grain without visible structure, and it is computed as
/// much in parallel as Bayer.
///
/// Built with the void-and-cluster algorithm (Ulichney): take a sparse random set of
/// points, then keep moving the tightest point into the largest void until the layout
/// stops changing; the resulting prototype is ranked in both directions. Density is
/// measured with a Gaussian filter that wraps around the edges, so the matrix tiles
/// with itself seamlessly.
///
/// Computed once on first use: 4096 cells take a fraction of a millisecond.
const std::array<uint16_t, kBlueNoiseSize * kBlueNoiseSize>& blueNoiseMatrix()
{
    static const std::array<uint16_t, kBlueNoiseSize * kBlueNoiseSize> matrix = [] {
        constexpr int n = kBlueNoiseSize;
        constexpr int total = n * n;
        constexpr int radius = 5;          // beyond this the kernel is negligible
        constexpr float sigmaSq = 1.5f * 1.5f;

        // Gaussian filter kernel
        std::array<float, (2 * radius + 1) * (2 * radius + 1)> kernel{};
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
                kernel[size_t((dy + radius) * (2 * radius + 1) + (dx + radius))] =
                    std::exp(-float(dx * dx + dy * dy) / (2.f * sigmaSq));

        std::vector<uint8_t> pattern(total, 0);
        std::vector<float> energy(total, 0.f);

        // Add or remove a point's contribution to the density map
        const auto stamp = [&](int index, float sign) {
            const int cy = index / n;
            const int cx = index % n;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int y = ((cy + dy) % n + n) % n;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int x = ((cx + dx) % n + n) % n;
                    energy[size_t(y * n + x)] +=
                        sign * kernel[size_t((dy + radius) * (2 * radius + 1) + (dx + radius))];
                }
            }
        };

        // The tightest cluster among occupied cells and the largest void among free ones
        const auto tightestCluster = [&] {
            int best = -1;
            float bestValue = -std::numeric_limits<float>::max();
            for (int i = 0; i < total; ++i)
                if (pattern[size_t(i)] && energy[size_t(i)] > bestValue) {
                    bestValue = energy[size_t(i)];
                    best = i;
                }
            return best;
        };
        const auto largestVoid = [&] {
            int best = -1;
            float bestValue = std::numeric_limits<float>::max();
            for (int i = 0; i < total; ++i)
                if (!pattern[size_t(i)] && energy[size_t(i)] < bestValue) {
                    bestValue = energy[size_t(i)];
                    best = i;
                }
            return best;
        };

        // Initial layout: a tenth of the cells, deterministic generator
        Rng rng(12345u);
        int placed = 0;
        const int initialPoints = total / 10;
        while (placed < initialPoints) {
            const int i = int(rng.next() % uint32_t(total));
            if (pattern[size_t(i)])
                continue;
            pattern[size_t(i)] = 1;
            stamp(i, 1.f);
            ++placed;
        }

        // Move clusters into voids until the swap starts to cycle
        for (;;) {
            const int cluster = tightestCluster();
            pattern[size_t(cluster)] = 0;
            stamp(cluster, -1.f);
            const int voidIndex = largestVoid();
            if (voidIndex == cluster) {
                pattern[size_t(cluster)] = 1;
                stamp(cluster, 1.f);
                break;
            }
            pattern[size_t(voidIndex)] = 1;
            stamp(voidIndex, 1.f);
        }

        std::array<uint16_t, total> rank{};
        const std::vector<uint8_t> prototype = pattern;

        // Phase 1: take the prototype apart, the tightest point gets the highest rank
        for (int r = placed - 1; r >= 0; --r) {
            const int cluster = tightestCluster();
            pattern[size_t(cluster)] = 0;
            stamp(cluster, -1.f);
            rank[size_t(cluster)] = uint16_t(r);
        }

        // Phase 2: restore the prototype and fill the voids with higher ranks
        pattern = prototype;
        std::fill(energy.begin(), energy.end(), 0.f);
        for (int i = 0; i < total; ++i)
            if (pattern[size_t(i)])
                stamp(i, 1.f);
        for (int r = placed; r < total; ++r) {
            const int voidIndex = largestVoid();
            pattern[size_t(voidIndex)] = 1;
            stamp(voidIndex, 1.f);
            rank[size_t(voidIndex)] = uint16_t(r);
        }
        return rank;
    }();
    return matrix;
}

/// Limit of the accumulated diffusion error per channel, in RGB units.
///
/// The nearest color is searched in the chosen color model while the residual is
/// computed in RGB. As long as the palette can reproduce the pixel color, the sign of
/// the residual alternates and it cancels out. But if the palette lacks the color
/// altogether (say, a color photo and a grayscale palette), the error has the same sign
/// every time, grows without bound and starts dragging luminance along with it,
/// wrecking the picture. The limit breaks this feedback without disturbing ordinary
/// diffusion: on the test image a color photo in a gray palette improves by 7 %
/// (ΔE2000 over 4×4 blocks), and repainting into ordinary color palettes by 5 %.
constexpr float kMaxDiffusedError = 96.f;

inline float clampError(float e)
{
    return std::clamp(e, -kMaxDiffusedError, kMaxDiffusedError);
}

/// Error diffusion with an arbitrary kernel. Every row depends on the errors of the
/// previous ones, so the pass is sequential; it runs on the orchestrator thread and
/// reports progress per row.
///
/// The scan is serpentine: even rows left to right, odd rows right to left, with the
/// horizontal kernel offsets mirrored on the way back. With a one-way scan the error
/// keeps drifting to the right and leaves a characteristic slanted trail; the serpentine
/// removes it because the drift direction alternates from row to row.
QImage errorDiffusion(const QImage& in, const PaletteMapper& mapper, Dithering dithering,
                      QPromise<QImage>& promise)
{
    const int width = in.width();
    const int height = in.height();
    QImage out(in.size(), QImage::Format_ARGB32);

    const QList<DiffusionWeight> kernelList = diffusionKernel(dithering);
    const std::vector<DiffusionWeight> kernel(kernelList.begin(), kernelList.end());
    int rowsAhead = 1;
    int reach = 1;
    for (const DiffusionWeight& w : kernel) {
        rowsAhead = std::max(rowsAhead, w.dy);
        reach = std::max(reach, std::abs(w.dx));
    }

    // Ring of error buffers: the current row and the ones the kernel reaches ahead to.
    // Each side has a margin of reach pixels so the loop needs no bounds checks.
    const int pad = reach;
    const size_t stride = size_t(width + 2 * pad) * 3;
    std::vector<std::vector<float>> rows(size_t(rowsAhead + 1), std::vector<float>(stride, 0.f));
    size_t head = 0;

    // Pointers to the ring rows by dy offset, so the hot loop has no division
    std::vector<float*> ahead(size_t(rowsAhead + 1), nullptr);

    for (int y = 0; y < height; ++y) {
        if (promise.isCanceled())
            return {};

        const QRgb* s = constRow(in, y);
        auto* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        const bool leftToRight = (y & 1) == 0;

        for (int dy = 0; dy <= rowsAhead; ++dy)
            ahead[size_t(dy)] = rows[(head + size_t(dy)) % rows.size()].data();
        float* const cur = ahead[0];

        for (int step = 0; step < width; ++step) {
            const int x = leftToRight ? step : width - 1 - step;
            const size_t i = size_t(x + pad) * 3;

            const float r = qRed(s[x]) + clampError(cur[i]);
            const float g = qGreen(s[x]) + clampError(cur[i + 1]);
            const float b = qBlue(s[x]) + clampError(cur[i + 2]);

            const QRgb wanted = qRgb(colorspace::clampByte(r), colorspace::clampByte(g), colorspace::clampByte(b));
            const QRgb chosen = mapper.nearest(wanted);
            d[x] = (chosen & 0x00FFFFFFu) | (s[x] & 0xFF000000u);

            const float er = r - qRed(chosen);
            const float eg = g - qGreen(chosen);
            const float eb = b - qBlue(chosen);

            for (const DiffusionWeight& w : kernel) {
                const int nx = x + (leftToRight ? w.dx : -w.dx);
                if (nx < 0 || nx >= width)
                    continue;
                float* target = ahead[size_t(w.dy)] + size_t(nx + pad) * 3;
                target[0] += er * w.weight;
                target[1] += eg * w.weight;
                target[2] += eb * w.weight;
            }
        }

        // The finished row becomes the farthest one and is cleared
        std::fill(rows[head].begin(), rows[head].end(), 0.f);
        head = (head + 1) % rows.size();
        promise.setProgressValue(int(qint64(100) * (y + 1) / height));
    }
    return out;
}

} // namespace

float orderedDitherOffset(Dithering d, int x, int y, int channel)
{
    // Pattern shift per channel: a third and two thirds of the period
    static constexpr int kPhase2[3][2] = {{0, 0}, {0, 1}, {1, 0}};
    static constexpr int kPhase4[3][2] = {{0, 0}, {1, 2}, {2, 1}};
    static constexpr int kPhase8[3][2] = {{0, 0}, {2, 4}, {4, 2}};
    static constexpr int kPhaseNoise[3][2] = {{0, 0}, {21, 42}, {42, 21}};

    const int c = std::clamp(channel, 0, 2);
    switch (d) {
    case Dithering::Bayer2:
        return kBayer2[(y + kPhase2[c][1]) & 1][(x + kPhase2[c][0]) & 1] / 4.f - 0.5f;
    case Dithering::Bayer4:
        return kBayer4[(y + kPhase4[c][1]) & 3][(x + kPhase4[c][0]) & 3] / 16.f - 0.5f;
    case Dithering::Bayer8:
        return kBayer8[(y + kPhase8[c][1]) & 7][(x + kPhase8[c][0]) & 7] / 64.f - 0.5f;
    case Dithering::BlueNoise: {
        const int ny = (y + kPhaseNoise[c][1]) & (kBlueNoiseSize - 1);
        const int nx = (x + kPhaseNoise[c][0]) & (kBlueNoiseSize - 1);
        return blueNoiseMatrix()[size_t(ny * kBlueNoiseSize + nx)] / float(kBlueNoiseSize * kBlueNoiseSize) - 0.5f;
    }
    default:
        return 0.f;
    }
}

bool isOrderedDithering(Dithering d)
{
    return d == Dithering::Bayer2 || d == Dithering::Bayer4 || d == Dithering::Bayer8 ||
           d == Dithering::BlueNoise;
}

bool isErrorDiffusion(Dithering d)
{
    return d == Dithering::FloydSteinberg || d == Dithering::Atkinson || d == Dithering::Sierra ||
           d == Dithering::Stucki || d == Dithering::JarvisJudiceNinke;
}

QList<DiffusionWeight> diffusionKernel(Dithering d)
{
    // Weights are integer shares of a common denominator, which makes them easy to check against the literature
    const auto build = [](std::initializer_list<std::array<int, 3>> items, float denominator) {
        QList<DiffusionWeight> kernel;
        for (const std::array<int, 3>& item : items)
            kernel.append(DiffusionWeight{item[0], item[1], float(item[2]) / denominator});
        return kernel;
    };

    switch (d) {
    case Dithering::FloydSteinberg:
        return build({{1, 0, 7}, {-1, 1, 3}, {0, 1, 5}, {1, 1, 1}}, 16.f);
    case Dithering::Atkinson:
        // Only 6/8 of the error is spread: the rest is lost, which raises contrast and
        // keeps fine transitions from smearing; the signature look of the classic Macintosh
        return build({{1, 0, 1}, {2, 0, 1}, {-1, 1, 1}, {0, 1, 1}, {1, 1, 1}, {0, 2, 1}}, 8.f);
    case Dithering::Sierra:
        return build({{1, 0, 5},
                      {2, 0, 3},
                      {-2, 1, 2},
                      {-1, 1, 4},
                      {0, 1, 5},
                      {1, 1, 4},
                      {2, 1, 2},
                      {-1, 2, 2},
                      {0, 2, 3},
                      {1, 2, 2}},
                     32.f);
    case Dithering::Stucki:
        return build({{1, 0, 8},
                      {2, 0, 4},
                      {-2, 1, 2},
                      {-1, 1, 4},
                      {0, 1, 8},
                      {1, 1, 4},
                      {2, 1, 2},
                      {-2, 2, 1},
                      {-1, 2, 2},
                      {0, 2, 4},
                      {1, 2, 2},
                      {2, 2, 1}},
                     42.f);
    case Dithering::JarvisJudiceNinke:
        return build({{1, 0, 7},
                      {2, 0, 5},
                      {-2, 1, 3},
                      {-1, 1, 5},
                      {0, 1, 7},
                      {1, 1, 5},
                      {2, 1, 3},
                      {-2, 2, 1},
                      {-1, 2, 3},
                      {0, 2, 5},
                      {1, 2, 3},
                      {2, 2, 1}},
                     48.f);
    default:
        return {};
    }
}

OrderedDither::OrderedDither(Dithering dithering, float strength)
{
    m_period = dithering == Dithering::BlueNoise ? kBlueNoiseSize : 8;
    m_mask = m_period - 1;
    m_table.resize(size_t(m_period) * size_t(m_period) * 3);
    for (int y = 0; y < m_period; ++y)
        for (int x = 0; x < m_period; ++x)
            for (int c = 0; c < 3; ++c)
                m_table[size_t((y * m_period + x) * 3 + c)] = orderedDitherOffset(dithering, x, y, c) * strength;
}

QString ditheringName(Dithering d)
{
    switch (d) {
    case Dithering::None:              return QObject::tr("No dithering");
    case Dithering::Bayer2:            return QObject::tr("Bayer 2×2");
    case Dithering::Bayer4:            return QObject::tr("Bayer 4×4");
    case Dithering::Bayer8:            return QObject::tr("Bayer 8×8");
    case Dithering::BlueNoise:         return QObject::tr("Blue noise");
    case Dithering::FloydSteinberg:    return QObject::tr("Floyd–Steinberg");
    case Dithering::Atkinson:          return QObject::tr("Atkinson");
    case Dithering::Sierra:            return QObject::tr("Sierra");
    case Dithering::Stucki:            return QObject::tr("Stucki");
    case Dithering::JarvisJudiceNinke: return QObject::tr("Jarvis–Judice–Ninke");
    }
    return {};
}

QList<Dithering> allDitherings()
{
    return {Dithering::None,           Dithering::Bayer2, Dithering::Bayer4, Dithering::Bayer8,
            Dithering::BlueNoise,      Dithering::FloydSteinberg, Dithering::Atkinson,
            Dithering::Sierra,         Dithering::Stucki, Dithering::JarvisJudiceNinke};
}

PaletteMapper::PaletteMapper(const Palette& palette, ColorModel model)
    : m_model(model)
    , m_cache(new std::atomic<uint16_t>[kCacheSize]())
{
    m_colors.reserve(size_t(palette.size()));
    m_modelColors.reserve(size_t(palette.size()));
    for (QRgb c : palette.colors()) {
        m_colors.push_back(c | 0xFF000000u);
        m_modelColors.push_back(colorspace::toModel(c, model));
    }
    if (m_colors.empty()) {
        m_colors.push_back(qRgb(0, 0, 0));
        m_modelColors.push_back(colorspace::toModel(qRgb(0, 0, 0), model));
    }
}

int PaletteMapper::nearestIndex(QRgb rgb) const
{
    const size_t key = cacheKey(rgb);
    const uint16_t cached = m_cache[key].load(std::memory_order_relaxed);
    if (cached != 0)
        return cached - 1;

    const Vec3 v = colorspace::toModel(cacheCenter(key), m_model);
    int best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    const size_t n = m_modelColors.size();
    for (size_t i = 0; i < n; ++i) {
        const float d = colorspace::distanceSq(v, m_modelColors[i]);
        if (d < bestDistance) {
            bestDistance = d;
            best = int(i);
        }
    }

    m_cache[key].store(uint16_t(best + 1), std::memory_order_relaxed);
    return best;
}

ImageOp PaletteMapper::makeOp(const Palette& palette, const RepaintParams& params)
{
    return [palette, params](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage in = toArgb32(source);
        const PaletteMapper mapper(palette, params.model);

        if (isErrorDiffusion(params.dithering))
            return errorDiffusion(in, mapper, params.dithering, promise);

        if (params.dithering == Dithering::None) {
            return mapPixels(in, promise, {0, 100}, [&](QRgb c) {
                return (mapper.nearest(c) & 0x00FFFFFFu) | (c & 0xFF000000u);
            });
        }

        // Ordered dithering: the Bayer or blue-noise threshold is added to the channels.
        // Each channel has its own threshold with a phase-shifted pattern, so dithering
        // renders not only intermediate luminance but intermediate hues as well.
        QImage out(in.size(), QImage::Format_ARGB32);
        const RowWriter writer(out);
        const int width = in.width();
        const OrderedDither dither(params.dithering, float(params.ditherStrength));

        parallelRows(in.height(), promise, {0, 100}, [&](int y) {
            const QRgb* s = constRow(in, y);
            QRgb* dst = writer.row(y);
            for (int x = 0; x < width; ++x) {
                const QRgb wanted = qRgb(colorspace::clampByte(qRed(s[x]) + dither.at(x, y, 0)),
                                         colorspace::clampByte(qGreen(s[x]) + dither.at(x, y, 1)),
                                         colorspace::clampByte(qBlue(s[x]) + dither.at(x, y, 2)));
                dst[x] = (mapper.nearest(wanted) & 0x00FFFFFFu) | (s[x] & 0xFF000000u);
            }
        });
        return promise.isCanceled() ? QImage() : out;
    };
}

} // namespace kolorit
