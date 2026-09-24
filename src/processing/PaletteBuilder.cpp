#include "processing/PaletteBuilder.h"

#include "core/ColorSpace.h"
#include "core/ParallelFor.h"
#include "core/PixelOps.h"
#include "core/Random.h"

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace kolorit::palette_builder {

Palette fromChunks(const QImage& source, int columns, int rows)
{
    Palette palette;
    if (source.isNull())
        return palette;

    const QImage image = toArgb32(source);
    columns = std::clamp(columns, 1, std::max(1, image.width()));
    rows = std::clamp(rows, 1, std::max(1, image.height()));

    // Chunk size in pixels (the last chunks in a row or column may be partial)
    const int chunkW = std::max(1, image.width() / columns);
    const int chunkH = std::max(1, image.height() / rows);

    std::vector<QRgb> averages(size_t(columns) * size_t(rows));
    NoProgress progress;

    parallelRows(rows, progress, {}, [&](int cy) {
        const int y0 = cy * chunkH;
        const int y1 = std::min(image.height(), y0 + chunkH);
        for (int cx = 0; cx < columns; ++cx) {
            const int x0 = cx * chunkW;
            const int x1 = std::min(image.width(), x0 + chunkW);
            // Averaging in sRGB: measurements show a palette of linear-light averages is
            // worse (repaint ΔE2000 5.19 vs 4.79), and Lab is better by less than a percent
            quint64 r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; ++y) {
                const QRgb* row = constRow(image, y);
                for (int x = x0; x < x1; ++x) {
                    r += qRed(row[x]);
                    g += qGreen(row[x]);
                    b += qBlue(row[x]);
                    ++n;
                }
            }
            averages[size_t(cy) * size_t(columns) + size_t(cx)] =
                n ? qRgb(int(r / n), int(g / n), int(b / n)) : qRgb(0, 0, 0);
        }
    });

    for (QRgb c : averages)
        palette.addUnique(c);
    return palette;
}

Palette uniqueColors(const QImage& image, int maxColors)
{
    return Palette::fromImage(image, maxColors);
}

Palette reduce(const Palette& palette, int count, uint32_t seed)
{
    const int wanted = std::clamp(count, 1, 256);
    if (palette.size() <= wanted)
        return palette;

    // The points to cluster are the colors of the palette itself, converted to Lab
    std::vector<Vec3> points;
    points.reserve(size_t(palette.size()));
    for (QRgb c : palette.colors())
        points.push_back(colorspace::toModel(c, ColorModel::Lab));

    std::vector<Vec3> centers;
    centers.reserve(size_t(wanted));

    // k-means++ initialization: the first center is random, the next ones are picked
    // proportionally to the squared distance to the nearest center chosen so far
    Rng rng(seed);
    centers.push_back(points[size_t(rng.next() % uint32_t(points.size()))]);
    std::vector<float> minDist(points.size(), std::numeric_limits<float>::max());
    while (int(centers.size()) < wanted) {
        double sum = 0.0;
        for (size_t i = 0; i < points.size(); ++i) {
            minDist[i] = std::min(minDist[i], colorspace::distanceSq(points[i], centers.back()));
            sum += minDist[i];
        }
        if (sum <= 0.0)
            break;   // all remaining colors coincide with the chosen ones
        double target = rng.unit() * sum;
        size_t chosen = points.size() - 1;
        for (size_t i = 0; i < points.size(); ++i) {
            target -= minDist[i];
            if (target <= 0.0) {
                chosen = i;
                break;
            }
        }
        centers.push_back(points[chosen]);
    }

    // Lloyd iterations; palettes are small, so the pass is synchronous
    std::vector<int> assignment(points.size(), 0);
    for (int iteration = 0; iteration < 50; ++iteration) {
        bool moved = false;
        for (size_t i = 0; i < points.size(); ++i) {
            int best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for (size_t c = 0; c < centers.size(); ++c) {
                const float d = colorspace::distanceSq(points[i], centers[c]);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = int(c);
                }
            }
            if (assignment[i] != best) {
                assignment[i] = best;
                moved = true;
            }
        }
        if (!moved && iteration > 0)
            break;

        std::vector<Vec3> sums(centers.size());
        std::vector<int> counts(centers.size(), 0);
        for (size_t i = 0; i < points.size(); ++i) {
            Vec3& acc = sums[size_t(assignment[i])];
            acc.x += points[i].x;
            acc.y += points[i].y;
            acc.z += points[i].z;
            ++counts[size_t(assignment[i])];
        }
        for (size_t c = 0; c < centers.size(); ++c)
            if (counts[c] > 0)
                centers[c] = Vec3{sums[c].x / counts[c], sums[c].y / counts[c], sums[c].z / counts[c]};
    }

    // A cluster is represented by its real color closest to the center, so the result
    // contains only colors that were in the palette
    Palette result;
    for (size_t c = 0; c < centers.size(); ++c) {
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < points.size(); ++i) {
            if (assignment[i] != int(c))
                continue;
            const float d = colorspace::distanceSq(points[i], centers[c]);
            if (d < bestDistance) {
                bestDistance = d;
                best = int(i);
            }
        }
        if (best >= 0)
            result.addUnique(palette.at(best));
    }
    return result;
}

ImageOp kMeansOp(int k, int iterations, uint32_t seed)
{
    return [k, iterations, seed](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const QImage image = toArgb32(source);
        if (image.isNull())
            return {};

        const int width = image.width();
        const int height = image.height();
        const int clusters = std::clamp(k, 1, 256);

        // --- Working sample in CIE Lab coordinates ---
        //
        // Clustering happens in a perceptually uniform space: the distance between points
        // there matches the visible difference between colors, so the cluster centers land
        // where the eye really tells shades apart. In RGB clusters are skewed towards blue,
        // which contributes a third of the distance for 7 % of the luminance.
        //
        // Conversion to Lab needs a cube root per channel, so it is done once per point
        // rather than on every iteration. To avoid holding 12 bytes for every pixel of a
        // large photo, the sample is thinned with a constant step; for a palette of a
        // handful of colors that makes no difference.
        constexpr qint64 kMaxSamples = 2000000;
        const qint64 total = qint64(width) * height;
        const int step = int(std::max<qint64>(1, (total + kMaxSamples - 1) / kMaxSamples));

        // The first sample of a row depends on the row number; otherwise, with a step
        // that divides the width, the same column would be picked every time. Row offsets
        // in the shared buffer are computed in advance, so every row writes to its own
        // range without locking.
        const auto firstColumn = [step, width](int y) {
            const int first = int((step - (qint64(y) * width) % step) % step);
            return first < width ? first : width;
        };
        std::vector<size_t> offsets(size_t(height) + 1, 0);
        for (int y = 0; y < height; ++y) {
            const int first = firstColumn(y);
            offsets[size_t(y) + 1] = offsets[size_t(y)] + size_t((width - first + step - 1) / step);
        }

        std::vector<Vec3> points(offsets[size_t(height)]);
        if (points.empty())
            return {};

        Vec3* const pointData = points.data();
        parallelRows(height, promise, {0, 10}, [&, pointData](int y) {
            const QRgb* row = constRow(image, y);
            Vec3* out = pointData + offsets[size_t(y)];
            for (int x = firstColumn(y); x < width; x += step) {
                colorspace::rgbToLab(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), out->x, out->y, out->z);
                ++out;
            }
        });
        if (promise.isCanceled())
            return {};

        std::vector<Vec3> centers(static_cast<size_t>(clusters));

        // --- k-means++ initialization on a subsample of the points ---
        Rng rng(seed);
        std::vector<const Vec3*> sample;
        {
            const size_t wanted = 20000;
            const size_t sampleStep = std::max<size_t>(1, points.size() / wanted);
            sample.reserve(points.size() / sampleStep + 1);
            for (size_t i = 0; i < points.size(); i += sampleStep)
                sample.push_back(&points[i]);
        }
        std::vector<float> minDist(sample.size(), std::numeric_limits<float>::max());
        centers[0] = *sample[size_t(rng.next() % uint32_t(sample.size()))];
        for (int c = 1; c < clusters; ++c) {
            const Vec3& last = centers[size_t(c - 1)];
            double sum = 0.0;
            for (size_t i = 0; i < sample.size(); ++i) {
                minDist[i] = std::min(minDist[i], colorspace::distanceSq(*sample[i], last));
                sum += minDist[i];
            }
            // Pick proportionally to the squared distance to the nearest center
            double target = rng.unit() * sum;
            size_t chosen = sample.size() - 1;
            for (size_t i = 0; i < sample.size(); ++i) {
                target -= minDist[i];
                if (target <= 0.0) {
                    chosen = i;
                    break;
                }
            }
            centers[size_t(c)] = *sample[chosen];
        }

        // --- Iterations: assign points (in parallel) and recompute the centers ---
        struct Accum {
            double x = 0, y = 0, z = 0;
            quint64 n = 0;
        };
        std::vector<Accum> totals(static_cast<size_t>(clusters));
        QMutex mergeMutex;

        // Points are split into blocks, one per source row, so the granularity of the
        // parallel pass stays the same
        const size_t blockSize = points.size() / size_t(std::max(1, height)) + 1;
        const int blocks = int((points.size() + blockSize - 1) / blockSize);

        const int iters = std::max(1, iterations);
        for (int it = 0; it < iters; ++it) {
            if (promise.isCanceled())
                return {};

            std::fill(totals.begin(), totals.end(), Accum{});
            const ProgressSpan span{10 + int(qint64(90) * it / iters), 10 + int(qint64(90) * (it + 1) / iters)};

            parallelRows(blocks, promise, span, [&](int block) {
                // Local sums of the block, then a single merge under the mutex
                std::vector<Accum> local(static_cast<size_t>(clusters));
                const size_t from = size_t(block) * blockSize;
                const size_t to = std::min(points.size(), from + blockSize);
                for (size_t i = from; i < to; ++i) {
                    const Vec3& p = points[i];
                    int best = 0;
                    float bestD = std::numeric_limits<float>::max();
                    for (int c = 0; c < clusters; ++c) {
                        const float d = colorspace::distanceSq(p, centers[size_t(c)]);
                        if (d < bestD) {
                            bestD = d;
                            best = c;
                        }
                    }
                    Accum& a = local[size_t(best)];
                    a.x += p.x;
                    a.y += p.y;
                    a.z += p.z;
                    ++a.n;
                }
                QMutexLocker lock(&mergeMutex);
                for (int c = 0; c < clusters; ++c) {
                    totals[size_t(c)].x += local[size_t(c)].x;
                    totals[size_t(c)].y += local[size_t(c)].y;
                    totals[size_t(c)].z += local[size_t(c)].z;
                    totals[size_t(c)].n += local[size_t(c)].n;
                }
            });

            float maxShift = 0.f;
            for (int c = 0; c < clusters; ++c) {
                const Accum& a = totals[size_t(c)];
                Vec3& center = centers[size_t(c)];
                if (a.n == 0) {
                    // Empty cluster: move its center to a random sample point
                    center = *sample[size_t(rng.next() % uint32_t(sample.size()))];
                    maxShift = std::numeric_limits<float>::max();
                    continue;
                }
                const Vec3 updated{float(a.x / double(a.n)), float(a.y / double(a.n)), float(a.z / double(a.n))};
                maxShift = std::max({maxShift, std::fabs(updated.x - center.x), std::fabs(updated.y - center.y),
                                     std::fabs(updated.z - center.z)});
                center = updated;
            }
            // Convergence threshold in Lab units: the L scale is 0..100 versus 0..255 in RGB
            if (maxShift < 0.2f)
                break;   // converged
        }

        if (promise.isCanceled())
            return {};

        // Sort clusters by size: the most frequent colors come first
        std::vector<int> order(static_cast<size_t>(clusters));
        for (int c = 0; c < clusters; ++c)
            order[size_t(c)] = c;
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return totals[size_t(a)].n > totals[size_t(b)].n; });

        Palette palette;
        for (int c : order) {
            const Vec3& center = centers[size_t(c)];
            palette.addUnique(colorspace::labToRgb(center.x, center.y, center.z));
        }
        promise.setProgressValue(100);
        return palette.toImage();
    };
}

} // namespace kolorit::palette_builder
